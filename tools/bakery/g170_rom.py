#!/usr/bin/env python3
"""Shared ROM/RAM helpers for the unbaked-model placement survey (gate G170)."""
import struct, json, os

_REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
# THE REPO IS THIS FILE'S OWN GRANDPARENT, not a path under some home directory. A
# hardcoded ./ is right on exactly one machine and silently wrong everywhere else,
# including a second checkout on this one.

ROM_PATH = os.environ.get("CNC3D_ROM", os.path.join(_REPO, "data", "rom", "cnc_eu.z64"))
ROM = open(ROM_PATH, "rb").read()

# resident boot segment: IPL3 copies ROM[0x1000..] -> RAM[0x80000400..]
RES = (0x1000, 0x90000, 0x80000400 - 0x1000)

# overlay table, tools/bakery/support/overlay_table.json:
#   [rec_rom, ?, rom_start, rom_end, ram_start, ram_end, ...]
_OV = json.load(open(os.path.join(_REPO, "tools", "bakery", "support", "overlay_table.json")))
OVERLAYS = []
for r in _OV:
    OVERLAYS.append((r[2], r[3], r[4] - r[2]))

# the one that holds the type-class constructors and the draw tables
CODE = (0x1643F0, 0x1B67D0, 0x801C2600 - 0x1643F0)   # delta 0x8005E210
NAMES = (0x1B67D0, 0x20E400, 0x801C2600 - 0x1B67D0)  # delta 0x8000BE30

def ram2rom(a, seg=CODE):
    return a - seg[2]

def rom2ram(o, seg=CODE):
    return o + seg[2]

def u32(o):  return struct.unpack_from(">I", ROM, o)[0]
def s32(o):  return struct.unpack_from(">i", ROM, o)[0]
def u16(o):  return struct.unpack_from(">H", ROM, o)[0]
def s16(o):  return struct.unpack_from(">h", ROM, o)[0]
def u8(o):   return ROM[o]

def cstr(o, seg=CODE):
    """Read a NUL-terminated string at a RAM address inside `seg`."""
    p = ram2rom(o, seg)
    e = ROM.find(b"\x00", p)
    return ROM[p:e].decode("latin1", "replace")
