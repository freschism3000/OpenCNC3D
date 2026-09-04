#!/usr/bin/env python3
"""WHERE THE UNBAKED CARTRIDGE MODELS BELONG, recovered from the ROM and asserted.

Run:  python3 tools/bakery/g170_unbaked_survey.py

Answers four questions that were open, and fails loudly rather than printing a
plausible lie if any address moves:

  1. THE CELL DECORATION DRAW TABLE (RAM 0x8020FF58, 39 records of {mode, descriptor}).
     Every mode-2 record is {bodyTypeId, shadowTypeId} and model slot = typeId + 10.
     Cross-joined with the 30 OverlayTypeClass and 15 SmudgeTypeClass registrations,
     each of which passes its draw index as the third constructor argument, so every
     record gets the name of the game object that reaches it. This settles the crates.

  2. THE TERRAIN REGISTRATION (ctor RAM 0x801EAC0C, 32 calls). Each call passes three
     per-theater model ids, stored at object +0x26 desert, +0x28 temperate,
     +0x2A winter, and copied to +0x00 by the theater switch at RAM 0x801EAD94.

  3. THE DESERT FLORA SUBSTITUTION, which is why 19 model-table slots looked orphaned.
     The draw-command enqueue at RAM 0x8004A420 adds 97 to any model id in [103, 121]
     when the theater word at RAM 0x80097150 is zero, and theater zero is DESERT by the
     theater loader's own filename switch at RAM 0x801E5190. Ids 103..121 are exactly
     T01..T18, so in desert the eighteen tree types draw slots 210..228: four cactus
     and desert-scrub meshes instead of the temperate billboards.

  4. THE SECOND MODEL TABLE (RAM 0x8009A7EC, 19 records of 0x14 bytes), which is the
     vehicle and aircraft ground-shadow table and not a continuation of the first.
     Model-table "slot 233" does not exist; that display list is the buggy's shadow.
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import g170_rom as R                                            # noqa: E402
import g170_args as A                                           # noqa: E402

RES = 0x7FFFF400          # resident: rom = ram - RES

OVERLAY_CTOR = 0x801F2F20
SMUDGE_CTOR = 0x801E8CE0
TERRAIN_CTOR = 0x801EAC0C
DRAW_TABLE = 0x8020FF58
DRAW_RECORDS = 39
MODEL_TABLE = 0x9A598     # rom, stride 16, node ptr at +0x0C, slots 0..228
SHADOW_TABLE = 0x9B3EC    # rom, stride 0x14, node ptr at +0x0C, type id at +0x10


def s32(v):
    return v - 0x100000000 if v >= 0x80000000 else v


def registrations(lo, hi, ctor, name_arg=6, index_arg=7, stack_name=None):
    out = []
    for off, tgt, reg, st in A.scan(lo, hi):
        if tgt != ctor:
            continue
        p = st.get(stack_name) if stack_name is not None else reg.get(name_arg)
        nm = R.cstr(p) if p and 0x801C0000 < p < 0x80220000 else "?"
        out.append((off, reg.get(5), nm, reg, st))
    return out


def main():
    fail = 0

    print("== 1. cell decoration draw table, RAM 0x%08X ==" % DRAW_TABLE)
    claim = {}
    ovl = registrations(0x194D00, 0x196000, OVERLAY_CTOR)
    smu = registrations(0x18AC00, 0x18B200, SMUDGE_CTOR)
    assert len(ovl) == 30, "expected 30 OverlayTypeClass registrations, got %d" % len(ovl)
    assert len(smu) == 15, "expected 15 SmudgeTypeClass registrations, got %d" % len(smu)
    for off, tid, nm, reg, st in ovl + smu:
        di = s32(reg.get(7)) if reg.get(7) is not None else None
        if di is not None and di >= 0:
            claim.setdefault(di, []).append(nm)
    base = R.ram2rom(DRAW_TABLE)
    rows = []
    for i in range(DRAW_RECORDS):
        mode, desc = R.u32(base + i * 8), R.u32(base + i * 8 + 4)
        body = shadow = None
        if mode == 2:
            d = R.ram2rom(desc)
            body, shadow = s32(R.u32(d)), s32(R.u32(d + 4))
        who = ",".join(claim.get(i, [])) or "-"
        rows.append((i, mode, desc, body, shadow, who))
        slot = ("%d" % (body + 10)) if body is not None and body >= 0 else "-"
        sslot = ("%d" % (shadow + 10)) if shadow is not None and shadow >= 0 else "-"
        print("  %2d  mode=%d desc=0x%08X  body=%-5s shadow=%-5s  slot=%-4s shadowSlot=%-4s  %s"
              % (i, mode, desc, body, shadow, slot, sslot, who))

    def rec(i):
        return rows[i][3], rows[i][4]

    # The five walls are the calibration: each was proved independently from geometry.
    for nm, idx, want in (("SBAG", 33, (143, 147)), ("CYCL", 31, (151, 155)),
                          ("BRIK", 30, (166, 170)), ("BARB", 29, (174, -1)),
                          ("WOOD", 37, (178, 182))):
        assert rec(idx) == want, "%s draw record %d is %s, expected %s" % (nm, idx, rec(idx), want)
        assert nm in claim.get(idx, []), "%s does not claim draw index %d" % (nm, idx)
    assert rec(34) == (88, -1) and "SCRATE" in claim[34], "SCRATE is not draw index 34 / type 88"
    assert rec(36) == (90, -1) and "WCRATE" in claim[36], "WCRATE is not draw index 36 / type 90"
    assert rec(38) == (195, -1), "draw index 38 is not the type-195 wreck"
    unclaimed = [i for i in range(DRAW_RECORDS) if i not in claim]
    print("  unclaimed draw indices: %s" % unclaimed)
    assert unclaimed == [0, 22, 35, 38], \
        "expected 0, 22, 35 and 38 to be unclaimed, got %s" % unclaimed
    print("  CRATES SETTLED: SCRATE -> model slot 98, WCRATE -> model slot 100.")

    print()
    print("== 2. terrain registration, ctor RAM 0x%08X ==" % TERRAIN_CTOR)
    ter = []
    for off, tgt, reg, st in A.scan(0x18C800, 0x18E200):
        if tgt != TERRAIN_CTOR:
            continue
        p = st.get(0x34)
        nm = R.cstr(p) if p and 0x801C0000 < p < 0x80220000 else "?"
        d, t, w = (s32(st.get(k) or 0) if st.get(k) is not None else None
                   for k in (0x38, 0x3C, 0x40))
        ter.append((nm, d, t, w))
        f = lambda v: "-" if v is None or v < 0 else "%d/slot %d" % (v, v + 10)
        print("  %-8s desert=%-12s temperate=%-12s winter=%s" % (nm, f(d), f(t), f(w)))
    assert len(ter) == 32, "expected 32 TerrainTypeClass registrations, got %d" % len(ter)
    names = [r[0] for r in ter]
    assert names[:4] == ["T01", "T02", "T03", "T04"], "terrain order changed: %s" % names[:4]
    assert dict((n, d) for n, d, _t, _w in ter)["T04"] == 106, "T04 desert id is not 106"
    trees = set("T%02d" % k for k in range(1, 19))
    tree_ids = sorted(d if d >= 0 else t for n, d, t, _w in ter if n in trees)
    assert tree_ids == list(range(103, 111)) + list(range(112, 122)), \
        "T01..T18 are not ids 103..121 with 111 unused: %s" % tree_ids

    print()
    print("== 3. the desert flora substitution ==")
    # RAM 0x8004A420: if (theaterWord == 0 && 103 <= id < 122) id += 97;
    for ram, want in ((0x8004A430, 0x8C427150),   # lw   $v0, 0x7150($v0)
                      (0x8004A45C, 0x2A020067),   # slti $v0, $s0, 103
                      (0x8004A468, 0x2A02007A),   # slti $v0, $s0, 122
                      (0x8004A470, 0x26100061)):  # addiu $s0, $s0, 97
        got = struct.unpack_from(">I", R.ROM, ram - RES)[0]
        assert got == want, "RAM 0x%08X reads %08X, expected %08X" % (ram, got, want)
    for ram, want in ((0x801E5198, 0x10600005),   # beqz $v1, DESERT arm
                      (0x801E519C, 0x24020002)):  # addiu $v0, $zero, 2  (TEMPERATE)
        got = R.u32(R.ram2rom(ram))
        assert got == want, "RAM 0x%08X reads %08X, expected %08X" % (ram, got, want)
    assert R.cstr(0x801C3C14) == "DESERT.TL4", "theater 0 does not load DESERT.TL4"
    assert R.cstr(0x801C3C2C) == "TEMPERAT.TL4", "theater 2 does not load TEMPERAT.TL4"
    print("  theater 0 = DESERT (loads DESERT.TL4/.TL8), theater 2 = TEMPERATE.")
    print("  in DESERT every model id in [103,121] is drawn as id + 97:")
    tnames = {103: "T01", 104: "T02", 105: "T03", 106: "T04", 107: "T05", 108: "T06",
              109: "T07", 110: "T08", 111: "(unused)", 112: "T09", 113: "T10",
              114: "T11", 115: "T12", 116: "T13", 117: "T14", 118: "T15",
              119: "T16", 120: "T17", 121: "T18"}
    desert_legal = {106, 110, 112, 121}
    for tid in range(103, 122):
        slot = tid + 97 + 10
        ptr = R.u32(MODEL_TABLE + 0xC + slot * 16)
        print("    id %3d %-9s -> id %3d -> model slot %3d  node 0x%08X%s"
              % (tid, tnames[tid], tid + 97, slot, ptr,
                 "   <- placed by shipped desert maps" if tid in desert_legal else ""))
    fams = {}
    for tid in range(103, 122):
        fams.setdefault(R.u32(MODEL_TABLE + 0xC + (tid + 107) * 16), []).append(tid + 107)
    assert len(fams) == 4, "expected 4 desert flora meshes over slots 210..228, got %d" % len(fams)
    print("  four distinct meshes over slots 210..228: %s"
          % {("0x%08X" % k): v for k, v in fams.items()})

    print()
    print("== 4. second model table (vehicle and aircraft ground shadows) ==")
    n = 0
    for i in range(19):
        o = SHADOW_TABLE + i * 0x14
        ptr, tid = R.u32(o + 0xC), s32(R.u32(o + 0x10))
        if ptr == 0 or tid < 0:
            continue
        n += 1
        print("  rec %2d  node 0x%08X  type %-3d -> the model at slot %d" % (i, ptr, tid, tid + 10))
    assert n == 17, "expected 17 live shadow records, got %d" % n
    assert R.u32(MODEL_TABLE + 0xC + 229 * 16) == 0, \
        "the primary model table does not end at slot 228"
    print("  the primary table ends at slot 228; there is no model slot 233.")

    print()
    print("ALL ASSERTIONS PASSED" if not fail else "FAILURES: %d" % fail)
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
