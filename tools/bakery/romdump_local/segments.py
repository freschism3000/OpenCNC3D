#!/usr/bin/env python3
"""
CNC3D — the overlay/segment table of Command & Conquer (N64).

THIS IS THE KEY TO ALL 3D GEOMETRY AND TEXTURES. Display lists address vertices and
textures by RAM address; the regions holding them are DMA'd overlays, so a ROM offset is
only meaningful together with the overlay it belongs to:

    ram = rom + delta        rom = ram - delta        delta = ramStart - romStart

Recovered by disassembling the loader at RAM 0x800559B4 (ROM 0x565B4), whose own debug
strings state the semantics:
    "Loading Overlay #%d, Number of Segments = %d"
    "Loading Segment #%d, Name = %s, ROM Size = %u, RAM Size = %u"

The loader's inner sequence proves the record layout:
    lw $a1,4($s0)    ; romStart      lw $a0,0x14($s0) ; bssStart
    lw $a2,8($s0)    ; romEnd        lw $a1,0x18($s0) ; bssEnd
    lw $a0,0xc($s0)  ; ramStart      -> bzero when they differ
    jal 0x80077958   ; dma(dst, src, len)
    subu $a2,$a2,$a1 ; len = romEnd - romStart   (branch-delay slot)

SegDesc (32 bytes, big-endian), 20 of them in resident .data at ROM 0x090DB4 / 0x09101C /
0x0911E8:
    +0x00 char* name      +0x10 u32 ramEnd
    +0x04 u32   romStart  +0x14 u32 bssStart
    +0x08 u32   romEnd    +0x18 u32 bssEnd
    +0x0C u32   ramStart  +0x1C void(*init)()

*** WHY EVERY EARLIER SEARCH MISSED THIS TABLE ***
romStart/romEnd are stored as PI **cart-physical** addresses `0xB0000000 | romOffset`,
not bare ROM offsets. Scans for raw {romStart, romEnd, ramStart} triplets can never match.
Mask with 0x0FFFFFFF.

Note: the game does NOT use N64 segment addressing. A whole-ROM scan for the G_SEGMENT
encoding returns zero hits and no display list contains opcode 0xDB; every G_VTX and
G_SETTIMG pointer is a plain KSEG0 0x80xxxxxx address.

*** OVERLAYS vs SEGMENTS — the distinction that matters for resolving pointers ***
Segments SHARE RAM SLOTS. Five slots are reused by multiple segments:
    0x80122B60  ScriptModels / Compress / MapSel / Score / IntroSequence / JoyConfig
    0x80130830  ReplayMission / FactionSel / DialogMenu
    0x8015A7A0  GameModelsGeometry / MovieTextures
    0x8017C100  Movie1..Movie6_1
    0x801C2600  CodeOverlay / BriefingCode
So a RAM address alone is AMBIGUOUS. What disambiguates it is the *overlay group*: an
overlay loads a specific SET of segments together. Those groups are listed in three
overlay managers, parsed below from the ROM (parse_overlays()) and cached in OVERLAYS.

A resolver that just walks the segment list in order gets this WRONG: GameModelsGeometry
is declared before MovieTextures and shares its slot, so it silently shadowed
MovieTextures and made all 70 movie texture pointers resolve into display-list opcode
bytes -- they decoded as coloured noise. Geometry was unaffected (0/887 G_VTX change),
but every movie texture was garbage. Always resolve within the DL's own overlay group.
"""
import struct

# name, romStart, romEnd, ramStart
SEGMENTS = [
    ("ScriptModels",       0x00C6EA0, 0x00DA420, 0x80122B60),
    ("GameModelsTextures", 0x00DA420, 0x00FE210, 0x801369B0),
    ("GameModelsGeometry", 0x00FE210, 0x01643F0, 0x8015A7A0),   # unit/building meshes
    ("CodeOverlay",        0x01643F0, 0x01B67D0, 0x801C2600),
    ("BriefingCode",       0x01B67D0, 0x020E400, 0x801C2600),
    ("Compress",           0x020E400, 0x021BB80, 0x80122B60),
    ("ReplayMission",      0x021BB80, 0x0224980, 0x80130830),
    ("FactionSel",         0x0224980, 0x0226360, 0x80130830),
    ("MapSel",             0x0226360, 0x0243B90, 0x80122B60),
    ("Score",              0x0243B90, 0x0250BF0, 0x80122B60),
    ("IntroSequence",      0x0250BF0, 0x0266700, 0x80122B60),   # C&C logo lives here
    ("JoyConfig",          0x0266700, 0x0267A50, 0x80122B60),
    ("DialogMenu",         0x0267A50, 0x026B350, 0x80130830),
    ("MovieTextures",      0x026B350, 0x028CCA0, 0x8015A7A0),
    ("Movie1",             0x028CCA0, 0x02D64A0, 0x8017C100),
    ("Movie2",             0x02D64A0, 0x030C250, 0x8017C100),
    ("Movie3",             0x030C250, 0x032D520, 0x8017C100),
    ("Movie4_1",           0x032D520, 0x03A1470, 0x8017C100),
    ("Movie5_1",           0x03A1470, 0x0442DA0, 0x8017C100),
    ("Movie6_1",           0x0442DA0, 0x04578A0, 0x8017C100),
]
BY_NAME = {s[0]: s for s in SEGMENTS}

# Overlay groups: each entry is the SET of segments loaded together. Parsed from the three
# overlay managers in resident .data (RAM 0x800903F0, 0x800905BC, 0x80090650); see
# parse_overlays() to re-derive from the ROM.
OVERLAYS = [
    ["ScriptModels"],
    ["ScriptModels", "GameModelsTextures", "GameModelsGeometry"],   # in-game
    ["Compress"],
    ["MapSel"], ["Score"], ["IntroSequence"], ["JoyConfig"],
    ["Compress", "DialogMenu"],
    ["Compress", "ReplayMission"],
    ["Compress", "FactionSel"],
    ["ScriptModels", "GameModelsTextures", "MovieTextures", "Movie1"],
    ["ScriptModels", "GameModelsTextures", "MovieTextures", "Movie2"],
    ["ScriptModels", "GameModelsTextures", "MovieTextures", "Movie3"],
    ["ScriptModels", "GameModelsTextures", "MovieTextures", "Movie4_1"],
    ["ScriptModels", "GameModelsTextures", "MovieTextures", "Movie5_1"],
    ["ScriptModels", "GameModelsTextures", "MovieTextures", "Movie6_1"],
    ["CodeOverlay"], ["BriefingCode"],
]

MANAGERS = (0x800903F0, 0x800905BC, 0x80090650)

def parse_overlays(rom):
    """Re-derive OVERLAYS from the ROM (the three overlay managers)."""
    def r2o(a): return a - 0x80000400 + 0x1000
    def u32(o): return struct.unpack_from(">I", rom, o)[0]
    def cstr(a):
        o = r2o(a); return rom[o:rom.find(b"\x00", o)].decode("latin1")
    groups = []
    for mgr in MANAGERS:
        o = r2o(mgr)
        num, tbl = u32(o + 4), u32(o + 8)
        for i in range(num):
            e = r2o(tbl) + i * 16
            nseg, sp = u32(e), u32(e + 4)
            if not (0 < nseg <= 16): continue
            names = []
            for k in range(nseg):
                dp = u32(r2o(sp) + k * 4)
                names.append(cstr(u32(r2o(dp))))
            groups.append(names)
    return groups

def delta(seg):
    _, romStart, _, ramStart = seg
    return (ramStart - romStart) & 0xFFFFFFFF

def segment_for_rom(off):
    for s in SEGMENTS:
        if s[1] <= off < s[2]:
            return s
    return None

def groups_for(seg_name):
    return [g for g in OVERLAYS if seg_name in g]

def make_resolver(dl_rom_off):
    """Resolve a G_VTX / G_SETTIMG RAM address to a ROM offset, correctly.

    Resolution is scoped to the overlay group(s) that actually load the display list's own
    segment, so segments sharing a RAM slot can never shadow one another. The DL's own
    segment is tried first, then its co-resident segments; unrelated segments are NOT
    consulted (that is exactly the bug this replaces).
    """
    own = segment_for_rom(dl_rom_off)
    order = []
    if own:
        order.append(own)
        for g in groups_for(own[0]):
            for n in g:
                s = BY_NAME[n]
                if s not in order:
                    order.append(s)
    else:
        order = list(SEGMENTS)

    def resolve(ram, numv=0):
        for _, romStart, romEnd, ramStart in order:
            if ramStart <= ram < ramStart + (romEnd - romStart):
                return ram - ramStart + romStart
        return None
    return resolve

if __name__ == "__main__":
    import sys
    print(f"{'segment':20s} {'romStart':>9s} {'romEnd':>9s} {'ramStart':>10s} {'delta':>10s}  size")
    for s in SEGMENTS:
        n, rs, re, ra = s
        print(f"{n:20s} 0x{rs:07X} 0x{re:07X} 0x{ra:08X} 0x{delta(s):08X}  {(re-rs)/1024:8.1f} KB")
    if len(sys.argv) > 1:
        rom = open(sys.argv[1], "rb").read()
        g = parse_overlays(rom)
        print(f"\n{len(g)} overlay groups parsed from ROM; matches baked-in table: {g == OVERLAYS}")
        for i, names in enumerate(g):
            print(f"  ov{i:2d}: {', '.join(names)}")
