#!/usr/bin/env python3
"""damage_art.py -- the cartridge's per-building DAMAGE ART, out of the ROM.

WHAT THIS IS. Every structure the N64 has a model for also has DAMAGE ART, and the rule
that selects it is byte for byte the 1995 rule: BuildingClass::Draw3D calls Health_Ratio()
and tests it against 0x80, i.e. half health. That art is not a damaged MESH and it is not a
fourth palette -- two earlier sweeps looked for exactly those two things, found neither,
and concluded the art did not exist. It is a standalone display list, referenced by nothing
in the scene graph, reachable only through one table, and APPENDED over the intact building
rather than replacing it.

THE CHAIN, and every address in it is asserted below before anything is written:

  ROM 0x03E934   sltiu $v0,$v0,0x80      Health_Ratio() < 0x80
  ROM 0x03E93C   sll  $s1,$v0,3          becomes draw-flag bit 3
  ROM 0x04D3E0   andi $v0,$s5,8          the model draw handler tests that bit
  ROM 0x006260   123-entry jump table    model index -> damage index (24 live, 99 default)
  ROM 0x0A42C0   24 x 24-byte records    +0x00 healthy emitter, +0x04 damaged DL stub,
                                         +0x08 damaged emitter, +0x14 forced anim frame

  record[+0x04] -> a two-command stub (gsSPDisplayList(real); gsSPEndDisplayList())
               -> the real damaged display list

DELIBERATELY NOT MODELLED: record +0x0C and +0x10. Every load in the damage path
(RAM 0x8004C640..0x8004C844) uses +0x00, +0x04, +0x08 and +0x14 and nothing else, so those
two fields are dead in the shipped build. Implementing them would be implementing behaviour
the cartridge does not have.

Writes tools/bakery/damage_art.json, the analogue of debris_chunks.json, so a bake never
needs the ROM. data/ still never enters git; this JSON is bakery data derived from it.
"""
import json, os, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import segments

ROM = os.path.join(HERE, "..", "..", "data", "rom", "cnc_eu.z64")
OUT = os.path.join(HERE, "..", "bakery", "damage_art.json")

RAM2ROM = 0x80000400 - 0x1000          # resident segment: rom = ram - this

def r32(rom, off):
    return struct.unpack_from(">I", rom, off)[0]

def ram2rom_resident(ram):
    return ram - RAM2ROM

# The 24 damage slots. Names are the engine's own StructType idents, in the order the ROM's
# jump table assigns them -- which is now READ AND ASSERTED below rather than asserted
# against nothing, which is what an earlier version of this comment claimed and did not do.
# Measured: model 39 (the GUNBOAT) takes damage slot 23 and model 122 takes slot 22, so the
# last two were the wrong way round when they were written by hand. Neither carries a
# display list, so nothing was ever baked from them, but a docstring that promises a
# cross-check has to perform one.
SLOT_IDENT = ["TMPL","EYE","WEAP","GTWR","ATWR","OBLI","GUN","FACT","PROC","SILO",
              "HPAD","HQ","SAM","AFLD","NUKE","NUK2","HOSP","BIO","PYLE","HAND",
              "FIX","MISS","SLOT132","GUNBOAT"]

# ROM 0x006260 (RAM 0x80005660): 123 arms, one per model index. A live arm loads a small
# damage index into $a1 (addiu $a1,$zero,N -- 0x2405xxxx -- or "move $a1,$zero" 0x00002821
# for zero) and falls into the shared tail; a dead arm jumps straight to RAM 0x800571EC,
# which loads -1 and returns NULL. Exactly 24 arms are live.
JUMP_TABLE_ROM = 0x006260
EXPECT_MODEL_TO_DAMAGE = {
    0:0, 1:1, 2:2, 3:3, 4:4, 5:5, 6:6, 7:7, 9:8, 11:9, 12:10, 13:11, 14:12, 15:13,
    16:14, 17:15, 18:16, 19:17, 20:18, 21:19, 22:20, 23:21, 39:23, 122:22,
}

EXPECT_DL_ROM = {
    0x0D49C0, 0x142208, 0x148668, 0x1454B8, 0x143658, 0x152420, 0x0D33B0, 0x118FE0,
    0x11D220, 0x146538, 0x0CE778, 0x14E830, 0x115D88, 0x107CA0, 0x110110, 0x109F58,
    0x145130, 0x150F88, 0x119F58, 0x112C70,
}

def main():
    rom = open(ROM, "rb").read()
    assert r32(rom, 0) == 0x80371240, "not a big-endian .z64"

    # ---- the four instructions that make this the 1995 rule ------------------------
    checks = [
        (0x03E934, 0x2C420080, "sltiu $v0,$v0,0x80 on Health_Ratio()"),
        (0x03E93C, 0x000288C0, "sll $s1,$v0,3 (the draw-flag bit)"),
        (0x04D3E0, 0x32A20008, "andi $v0,$s5,8 (the geometry arm's bit-3 test)"),
        (0x04D2C0, 0x32A20008, "andi $v0,$s5,8 (the animation arm's bit-3 test)"),
    ]
    for off, want, what in checks:
        got = r32(rom, off)
        assert got == want, "ROM 0x%06X is 0x%08X, expected 0x%08X (%s)" % (off, got, want, what)
        print("  ok  ROM 0x%06X = 0x%08X  %s" % (off, got, what))

    # ---- the jump table: model index -> damage index, READ, not assumed --------------
    m2d = {}
    for i in range(123):
        tgt = r32(rom, JUMP_TABLE_ROM + i * 4)
        trom = ram2rom_resident(tgt)
        if not (0 <= trom < len(rom) - 8):
            continue
        w0, w1 = r32(rom, trom), r32(rom, trom + 4)
        # a live arm is "addiu $a1,$zero,N" (0x2405xxxx), or "move $a1,$zero" (0x00002821)
        # for slot 0. Either can sit in the branch's delay slot, so check both words.
        idx = None
        for w in (w0, w1):
            if (w >> 16) == 0x2405:
                idx = w & 0xFFFF
            elif w == 0x00002821 and idx is None:
                idx = 0
        if idx is not None:
            m2d[i] = idx
    assert m2d == EXPECT_MODEL_TO_DAMAGE, (
        "the model-index jump table moved:\n  missing %s\n  extra   %s"
        % (sorted(set(EXPECT_MODEL_TO_DAMAGE.items()) - set(m2d.items())),
           sorted(set(m2d.items()) - set(EXPECT_MODEL_TO_DAMAGE.items()))))
    assert sorted(m2d.values()) == list(range(24)), \
        "the 24 damage slots are not a permutation of 0..23: %s" % sorted(m2d.values())
    print("  ok  jump table: %d live arms, damage slots 0..23, each used once" % len(m2d))

    # ---- the record array ----------------------------------------------------------
    REC = ram2rom_resident(0x800A36C0)
    assert REC == 0x0A42C0, "record array resolves to 0x%06X, expected 0x0A42C0" % REC

    rows, dls = [], set()
    for slot in range(24):
        base = REC + slot * 24
        healthy = r32(rom, base + 0x00)
        stub    = r32(rom, base + 0x04)
        damaged = r32(rom, base + 0x08)
        frame   = r32(rom, base + 0x14)
        row = {
            "slot": slot,
            "ident": SLOT_IDENT[slot],
            "healthy_emitter_ram": healthy,
            "stub_ram": stub,
            "damaged_emitter_ram": damaged,
            "forced_frame": frame,
            "dl_ram": 0,
            "dl_rom": 0,
        }
        if stub:
            # the stub is two commands: gsSPDisplayList(real) then gsSPEndDisplayList()
            srom = segments.make_resolver(ram2rom_resident(0x800A36C0))(stub)
            if srom is None:
                srom = ram2rom_resident(stub)
            w0, w1 = r32(rom, srom), r32(rom, srom + 4)
            assert (w0 >> 24) == 0xDE, ("slot %d stub at ROM 0x%06X opens 0x%08X, not a "
                                        "gsSPDisplayList" % (slot, srom, w0))
            row["dl_ram"] = w1
            res = segments.make_resolver(srom)
            drom = res(w1)
            if drom is None:
                drom = ram2rom_resident(w1)
            row["dl_rom"] = drom
            dls.add(drom)
        rows.append(row)

    with_dl = [r for r in rows if r["stub_ram"]]
    forced  = [(r["ident"], r["forced_frame"]) for r in rows if r["forced_frame"]]
    print("  %d slots, %d with a damaged display list, %d distinct lists"
          % (len(rows), len(with_dl), len(dls)))
    print("  forced animation frames: %s" % (forced,))

    assert len(with_dl) == 21, "expected 21 slots with a display list, got %d" % len(with_dl)
    assert len(dls) == 20, "expected 20 distinct display lists, got %d" % len(dls)
    assert dls == EXPECT_DL_ROM, ("the display-list set moved:\n  missing %s\n  extra   %s"
                                  % (sorted(EXPECT_DL_ROM - dls), sorted(dls - EXPECT_DL_ROM)))
    assert len(forced) == 3, "expected 3 forced frames, got %s" % (forced,)

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as f:
        json.dump({"note": "generated by tools/romdump/damage_art.py; do not hand-edit",
                   "records": rows}, f, indent=1)
    print("  wrote %s" % os.path.normpath(OUT))

if __name__ == "__main__":
    main()
