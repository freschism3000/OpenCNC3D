#!/usr/bin/env python3
"""
CNC3D -- the object graph: why several models were missing their moving part.

A model is not one display list. The 16-byte records of the model table at RAM
0x80099998 hold, in their last word, a pointer to a 0x1C-byte SCENE GRAPH NODE, and
that node can have children:

    node (0x1C bytes, big-endian, in GameModelsGeometry unless noted)
        +0x00 u32  gfx        display list for this part
        +0x04 u32  gfx alt    a second display list, same geometry, different skin
        +0x08 u32  behaviour  shared per part kind (turrets all point at 0x800AC2D8)
        +0x0C u32  children   NULL-terminated array of node pointers, or 0
        +0x10..+0x18          zero at rest (runtime scratch)

The exporter only ever read the root's +0x00, so every model came out as its static
body with the moving part dropped. That is exactly the GUN defect: slot 16 is right,
its root is the 19-triangle hexagonal base pad, and the 32-triangle barrel assembly it
should be wearing is its one child (RAM 0x801AE068, ROM 0x151AD8, bbox 426 x 126 x 779
model units -- long in z, which is a barrel).

The same child list gives the medium and light tanks their turrets, the Jeep and Buggy
their gun mounts, the Advanced Comm Center its dish, the helicopter its rotor disc and
the Weapons Factory its doors.

The first 12 bytes of each model-table record are zero in the ROM and are runtime
scratch, so there is NO per-model position offset stored anywhere in this table. That
matters for the V20 question: see the CHANGELOG.

Usage:
    python3 objgraph.py                 every slot that has children
    python3 objgraph.py <slot>          one slot's tree
"""
import os, struct, sys

HERE  = os.path.dirname(os.path.abspath(__file__))
ROOT  = os.path.abspath(os.path.join(HERE, "..", ".."))
ROM_P = os.path.join(ROOT, "rom", "cnc_eu.z64")

RES_DELTA = 0x80000400 - 0x1000              # resident: rom = ram - delta
GMG_DELTA = 0x8005C590                       # GameModelsGeometry: rom = ram - delta
GMG_LO, GMG_HI = 0x8015A7A0, 0x801C0980
MODEL_TABLE = 0x80099998
TABLE_SLOTS = 236
NODE_SIZE   = 0x1C
MAX_CHILDREN = 8                             # sanity cap on the NULL-terminated list

_ROM = None


def rom():
    global _ROM
    if _ROM is None:
        _ROM = open(ROM_P, "rb").read()
    return _ROM


def _in_gmg(a):
    return GMG_LO <= a < GMG_HI


def _r32_gmg(ram):
    return struct.unpack_from(">I", rom(), ram - GMG_DELTA)[0]


def node_ptr(slot):
    """The scene-graph node the model table points a slot at (0 if none/elsewhere)."""
    off = MODEL_TABLE - RES_DELTA + slot * 16 + 12
    p = struct.unpack_from(">I", rom(), off)[0]
    return p if _in_gmg(p) else 0


def walk_node(ram, depth=0):
    """[(depth, node_ram, gfx_ram, gfx_rom)] for this node and everything under it."""
    if not _in_gmg(ram) or depth > 4:
        return []
    gfx = _r32_gmg(ram)
    out = [(depth, ram, gfx, (gfx - GMG_DELTA) if _in_gmg(gfx) else None)]
    kids = _r32_gmg(ram + 0x0C)
    if _in_gmg(kids):
        for k in range(MAX_CHILDREN):
            cn = _r32_gmg(kids + k * 4)
            if cn == 0:
                break
            out += walk_node(cn, depth + 1)
    return out


def display_lists(slot):
    """Every display list a slot draws, root first. ROM offsets."""
    p = node_ptr(slot)
    if not p:
        return []
    return [rr for _d, _n, _g, rr in walk_node(p) if rr is not None]


if __name__ == "__main__":
    if len(sys.argv) > 1:
        slots = [int(sys.argv[1])]
    else:
        slots = range(TABLE_SLOTS)
    for s in slots:
        p = node_ptr(s)
        if not p:
            continue
        tree = walk_node(p)
        if len(tree) < 2 and len(sys.argv) == 1:
            continue                      # only show the interesting ones by default
        print("slot %3d  node %08X" % (s, p))
        for d, n, g, rr in tree:
            print("    %s node %08X gfx %08X  rom %s"
                  % ("  " * d, n, g, ("%07X" % rr) if rr is not None else "-"))
