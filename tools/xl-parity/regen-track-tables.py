#!/usr/bin/env python3
"""Regenerate drive.cpp's packed-hex track tables as XY_Delta(dx, dy) calls.

The classic engine's smooth-turn tracks (DriveClass::Track1..Track13 and the
jump tables around them) are COORDINATE deltas hand-packed as 32-bit hex:
X in the low 16 bits, Y in the high 16, each axis a signed 16-bit
two's-complement lepton delta. The XL brain's COORDINATE is 64-bit with 32-bit
axes (docs/design-xl-brain.md, coordDesign), so every one of those literals
would silently become "X = whole classic pattern, Y = 0" if left alone --
int-to-uint64 conversion COMPILES. This script decodes each literal into its
two signed axis deltas and rewrites it as a constexpr XY_Delta(dx, dy) call,
which packs correctly at ANY axis width. Per the contract: tables are never
hand-packed hex again.

One-shot against brain/xl/tiberiandawn/drive.cpp at the fork; kept for review
and for re-pinning the fork to a newer upstream.

    tools/xl-parity/regen-track-tables.py <file.cpp> [more files...]
"""
import re
import sys


def decode(lit):
    v = int(lit, 16)
    x = v & 0xFFFF
    y = (v >> 16) & 0xFFFF
    if x >= 0x8000:
        x -= 0x10000
    if y >= 0x8000:
        y -= 0x10000
    return x, y


def rewrite(text):
    # Only literals in table-entry position: "{0x........L," -- expression
    # sites elsewhere are reviewed by hand (they are few and contextual).
    pat = re.compile(r'\{0x([0-9A-Fa-f]{8})L,')

    def sub(m):
        x, y = decode(m.group(1))
        return "{XY_Delta(%d, %d)," % (x, y)

    return pat.subn(sub, text)


def main():
    for path in sys.argv[1:]:
        with open(path, encoding="latin-1") as f:
            text = f.read()
        text, n = rewrite(text)
        with open(path, "w", encoding="latin-1") as f:
            f.write(text)
        print("%s: %d packed track literals -> XY_Delta" % (path, n))


if __name__ == "__main__":
    main()
