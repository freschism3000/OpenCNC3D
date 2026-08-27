#!/usr/bin/env python3
"""Generate an all-clear PC-format C&C Tiberian Dawn .BIN for a 64x64 scenario.

Format (see MapClass::Read_Binary_File in tiberiandawn/map.cpp):
  4096 records, row-major over the 64x64 old-format cell grid, 2 bytes each:
     byte 0 = TemplateType (unsigned char); 255 == TEMPLATE_NONE == clear
     byte 1 = TIcon        (unsigned char)
  Total = 8192 bytes exactly. Read_Binary_File returns true only when all
  4096 records were read.
"""
import sys

args = sys.argv[1:]
size = 64                      # the legacy grid, when nothing says otherwise
if args and args[0] == "--size":
    if len(args) < 2 or not args[1].isdigit():
        raise SystemExit("usage: make_clear_bin.py [--size N] out.BIN ...")
    size = int(args[1])
    args = args[2:]
CELLS = size * size
data = bytes([0xFF, 0x00]) * CELLS
assert len(data) == CELLS * 2
for path in args:
    open(path, 'wb').write(data)
    print("wrote", path, len(data), "bytes (%dx%d)" % (size, size))
