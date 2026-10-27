#!/usr/bin/env python3
"""
bake_n64infantry.py -- the cartridge's infantry COMBAT strips, baked into
n64infantry.pack for the cartridge infantry art (FX_INF_N64, game/n64inf_mod.h).

WHY THIS EXISTS
    A scenario pack carries three strips per infantry set: STAND, RUN and DETH1. That is
    all the cartridge billboards could draw, so with the cartridge art chosen a soldier
    who fired stayed in his standing frame. The rifle's muzzle flash is not an effect of
    its own (WEAPON_M16 has ANIM_NONE in const.cpp); it is painted into the firing
    frames, so the flash went with them and a firefight could not be seen at all.

    The cartridge has those frames. Its 67 strips (tools/romdump/soldier.py) include
    SHOOT, CSHOT (prone fire) and CRAWL for the soldiers, SHOOT and CRAWL for the
    civilians, and PANIC for the two doctors. This pack carries them. It is a pack of its own rather than more slots in
    every scenario pack because it is read from committed data in under a second, so a
    build bakes it every time and no scenario pack has to be rebaked to carry it.

WHICH STRIP FOR WHICH DO IS THE CARTRIDGE'S OWN TABLE, read here rather than assumed.
    The sprite accessor at RAM 0x801FB600 (CodeOverlay, rom = ram - 0x8005E210) takes
    the engine's DoType and looks it up in a per-set byte table, one pointer per set at
    RAM 0x802102C4 in set order E1 E2 E3 E4 E6 RB MB DP. The byte is the strip's index
    within its set. The tables are indexed by DoType, which the death entries confirm:
    22 and 23 (gun, explosion) give DETH1, 24 and 25 give DETH2, 26 (fire) gives FDETH.
    For the four combat Dos this pack serves they read:

        DoType           E1 E2 E3 E4 RB     E6       MB       DP
        2  PRONE         CRAWL              CRAWL    STAND    STAND
        4  FIRE_WEAPON   SHOOT              STAND    STAND    SHOOT
        6  CRAWL         CRAWL              CRAWL    PANIC    CRAWL
        8  FIRE_PRONE    CSHOT              STAND    STAND    STAND

    A STAND (or RUN) answer is baked as -1: the renderer's own stand/run pick already
    draws those strips from the scenario pack.

    The same accessor fixes the STAGE: (engine stage % count) * stages / count, where
    count is the Do's frame count the caller passes in beside it. That is the 1995
    pack's src_stages rule too, so the renderer applies it with the count it already has.

AND WHERE THE CARD STANDS IS THE CARTRIDGE'S TOO. Its caller (RAM 0x801FC560) reads one
    byte per (type, DoType) from RAM 0x80212250, 34 bytes a type, and moves the quad's
    ground point by it before the quad is built (RAM 0x801FBCAC): y -= byte / 2 and
    z += byte - 5, in the 256-units-a-cell world, y up and z toward the camera. It is
    how a frame with empty rows under the body still lies on the ground: E1's prone
    fire is 71, his crawl 39, his standing fire 11 and his stand 0, against 5 to 9, 0
    to 5, 1 and 0 empty rows under the art. The table's row is the type the set
    belongs to (E5 reads E1's, CHAN Moebius's, every civilian C1's), which is what the
    type switch before the lookup does. The byte is baked per slot as `lift`; the -5
    every sprite gets is left out, because the stand and run draws do not take it.

OUTPUT FORMAT (little endian; game/n64inf_mod.h is the only reader)
    u8 magic[8] "N64INF01"; u32 version (1); u32 nstrip; u32 ntype
    per strip: char name[16] (e.g. "E1_SHOOT#0", the scenario pack's own naming)
               u32 frames, facings, fw, fh, texw, texh
               texw*texh RGBA bytes; frames stacked down from the top, facing-major,
               padded with transparent texels to a power of two
    per type:  char code[8]; int32 strip[2][4]; int32 lift[4]
               rows GDI, Nod (the scenario pack's two palettes, INFANTRY_SETS);
               slots PRONE, FIRE_WEAPON, CRAWL, FIRE_PRONE; -1 = keep the stand/run pick;
               lift is the cartridge's anchor byte for that slot (above), 0 where
               the strip is -1

The art is decoded by the PK4 baker's own bake_sprite, the function every scenario
pack's STAND/RUN/DETH1 strips come out of, so the two can never disagree on a texel.

Usage: python3 bake_n64infantry.py [out.pack]      (default: n64infantry.pack beside this)
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.normpath(os.path.join(HERE, "..", "tools", "bakery")))
import pk4mod  # noqa: E402

B = pk4mod.load()

CO_DELTA = 0x801C2600 - 0x1643F0       # CodeOverlay: ram = rom + delta
DO_TABLES = 0x802102C4                 # 8 pointers, one byte table per set
LIFTS = 0x80212250                     # one byte per (type row, DoType), 34 a row
SET_ORDER = ["E1", "E2", "E3", "E4", "E6", "RB", "MB", "DP"]
# The engine's DoType for each slot, in the order the renderer indexes them.
SLOTS = [2, 4, 6, 8]                   # PRONE, FIRE_WEAPON, CRAWL, FIRE_PRONE
KEEP = ("STAND", "RUN")                # the scenario pack already draws these


def ram_u32(ram):
    return struct.unpack_from(">I", B.ROM, ram - CO_DELTA)[0]


def strip_for(setname, do):
    """The strip name the cartridge draws for this set and DoType, from its own table."""
    table = ram_u32(DO_TABLES + 4 * SET_ORDER.index(setname))
    idx = B.ROM[table - CO_DELTA + do]
    names = [n for n, r in zip(B.SOL.names(), B.SOL.records()) if r["set"] == setname]
    return names[idx].split("_", 1)[1]


def lift_for(setname, do):
    """The cartridge's anchor byte for this set and DoType. The row is the first type id
    that wears the set, as the type switch at RAM 0x801FC5E4 resolves it."""
    row = min(t for t, (s, _p) in B.SOL.TYPE_MAP.items() if s == setname) - 2000
    return B.ROM[LIFTS - CO_DELTA + 34 * row + do]


def pow2(n):
    p = 1
    while p < n:
        p <<= 1
    return p


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "n64infantry.pack")

    # The table's own sanity, before anything is trusted: every set's STAND entry is
    # DoType 0, and the gun death is DETH1. A ROM read at the wrong address fails here.
    for s in SET_ORDER:
        assert strip_for(s, 0) == "STAND" and strip_for(s, 22) == "DETH1", s
    # and the anchor table's: nobody standing is moved, and E1 lies as measured above.
    assert all(lift_for(s, 0) == 0 for s in SET_ORDER)
    assert [lift_for("E1", d) for d in SLOTS] == [39, 11, 39, 71]

    strips, index, types = [], {}, []
    for code in sorted(B.INFANTRY_SETS):
        setname, pals = B.INFANTRY_SETS[code]
        rows = []
        lifts = [0 if strip_for(setname, do) in KEEP else lift_for(setname, do)
                 for do in SLOTS]
        for house in range(B.N_HOUSE):
            row = []
            for do in SLOTS:
                anim = strip_for(setname, do)
                if anim in KEEP:
                    row.append(-1)
                    continue
                nm = "%s_%s#%d" % (setname, anim, pals[house])
                if nm not in index:
                    w, h, rgba, n, fh = B.bake_sprite(setname, anim, pals[house])
                    rec = B.SOL.by_name()["%s_%s" % (setname, anim)]
                    assert n == rec["facings"] * rec["stages"] and h == n * fh, nm
                    tw, th = pow2(w), pow2(h)
                    buf = bytearray(tw * th * 4)
                    for y in range(h):
                        buf[y * tw * 4:(y * tw + w) * 4] = rgba[y * w * 4:(y + 1) * w * 4]
                    index[nm] = len(strips)
                    strips.append((nm, n, rec["facings"], w, fh, tw, th, bytes(buf)))
                row.append(index[nm])
            rows.append(row)
        types.append((code, rows, lifts))

    with open(out, "wb") as f:
        f.write(b"N64INF01")
        f.write(struct.pack("<III", 1, len(strips), len(types)))
        for nm, n, nfac, fw, fh, tw, th, buf in strips:
            f.write(nm.encode().ljust(16, b"\0"))
            f.write(struct.pack("<6I", n, nfac, fw, fh, tw, th))
            f.write(buf)
        for code, rows, lifts in types:
            f.write(code.encode().ljust(8, b"\0"))
            for row in rows:
                f.write(struct.pack("<4i", *row))
            f.write(struct.pack("<4i", *lifts))
    print("wrote %s: %d strips, %d infantry types (%.1f MB)"
          % (out, len(strips), len(types), os.path.getsize(out) / 1e6))
    for code, rows, lifts in types:
        print("  %-8s %s  lift %s" % (code, "  ".join(
            "/".join(strips[i][0] if i >= 0 else "-" for i in r) for r in rows), lifts))


if __name__ == "__main__":
    main()
