#!/usr/bin/env python3
"""The picture leg of the goodie-crate gates: what the crate pass put on the glass.

    python3 gate_crate3d.py ON.png OFF.png

Both shots are the SAME frame of SCG32EA at cam 7.0 60.0, zoom max, shroud off. ON draws
the crate pass; OFF has it switched out with the script's `crates 0`. Every number below
is the difference between the two, so nothing in the frame except the crate pass can move
one of them, and a pass that drew nothing at all reports zero rather than passing.

WHY THAT MISSION AND THAT FRAME. SCG32EA is the only kind of map that proves BOTH
bindings in one picture: its [OVERLAY] section places cell 3782 = SCRATE and cell
3783 = WCRATE, a steel crate at (6,59) with a wooden one immediately east of it. So the
left cluster must be the steel crate and the right one the wooden crate, and a renderer
that bound one mesh to both kinds fails on COLOUR rather than on size.

WHAT SEPARATES THE CUBE FROM THE SPRITE, measured on this machine off both renders:

    cube    steel 313 px, 16 x 21, mean rgb (83,82,80), r-b =  +3
            wood  312 px, 16 x 21, mean rgb (89,77,30), r-b = +59
    sprite  steel 646 px, 34 x 25, mean rgb (94,94,114), r-b = -20
            wood  646 px, 34 x 25, mean rgb (94,87,48),  r-b = +45

The cartridge's cube is 185 mesh units on a side, 0.18 of a cell at the renderer's cell
of 1024; the 1995 sprite is 10x11 of a 24-pixel DOS cell, 0.42 of a cell. So the cube is
the SMALLER of the two, which is the opposite of the intuition and is why the size band
below has a ceiling rather than a floor. The width band 12..24 is one the sprite's 34
cannot enter, and the steel colour leg is a second, independent witness: the cube's steel
is neutral grey (r-b = +3) where the 1995 steel sprite is blue-grey (r-b = -20).

WHAT THIS LEG CANNOT DO, said plainly because a gate that oversells itself is worse than
no gate. A picture cannot assert a POSITION: an earlier crate gate passed with the cube
floating half a cell in the air, because a generous window swallowed the shift. The
position is asserted in numbers from the renderer's own `cratedump` (ax, az and lift read
back out of draw_mesh), and this leg is only ever the second half of that pair.
"""
import sys

try:
    from PIL import Image
except ImportError:
    print("gate_crate3d: PIL is not installed")
    sys.exit(2)

# The two crates sit either side of this column in the gate's frame. Measured 80 pixels
# apart and 16 wide, so any split between them serves; this one is the midpoint.
SPLIT_X = 640
# A pixel counts as changed when its three channels differ by this much in total. Well
# under the crate's own contrast against grass (the same pixels read (31,57,26) with the
# pass off) and well over the shot's own dither.
THRESH = 12
# Generous windows: a correct crate may grow or shift a little without tripping the stray
# counter. Measured extents are x[592,607] and x[672,687], y[316,336].
WIN_Y = (280, 380)
WIN_X = ((560, 630), (650, 720))


def main(on_path, off_path):
    on = Image.open(on_path).convert("RGB")
    off = Image.open(off_path).convert("RGB")
    if on.size != off.size:
        print("CRATEPIX|error=size %s vs %s" % (on.size, off.size))
        return 2
    w, h = on.size
    pon, poff = on.load(), off.load()

    stats = [dict(n=0, x0=w, x1=-1, y0=h, y1=-1, r=0, g=0, b=0) for _ in range(2)]
    stray = 0
    for y in range(h):
        for x in range(w):
            a, o = pon[x, y], poff[x, y]
            if abs(a[0] - o[0]) + abs(a[1] - o[1]) + abs(a[2] - o[2]) <= THRESH:
                continue
            i = 0 if x < SPLIT_X else 1
            # A change far from either crate cell is a STRAY: the crate pass must not
            # touch the rest of the frame, and this is the only place a state leak out
            # of it would ever be caught.
            if not (WIN_Y[0] <= y <= WIN_Y[1]
                    and WIN_X[i][0] <= x <= WIN_X[i][1]):
                stray += 1
                continue
            s = stats[i]
            s["n"] += 1
            s["x0"] = min(s["x0"], x); s["x1"] = max(s["x1"], x)
            s["y0"] = min(s["y0"], y); s["y1"] = max(s["y1"], y)
            s["r"] += a[0]; s["g"] += a[1]; s["b"] += a[2]

    out = ["CRATEPIX", "stray=%d" % stray]
    ok = (stray == 0)
    means = []
    for i, nm in enumerate(("steel", "wood")):
        s = stats[i]
        if s["n"] == 0:
            out.append("%s_n=0 %s_w=0 %s_h=0 %s_rb=0" % (nm, nm, nm, nm))
            means.append(None)
            ok = False
            continue
        cw = s["x1"] - s["x0"] + 1
        ch = s["y1"] - s["y0"] + 1
        r = s["r"] // s["n"]; g = s["g"] // s["n"]; b = s["b"] // s["n"]
        means.append((r, g, b))
        out.append("%s_n=%d %s_w=%d %s_h=%d %s_rb=%d"
                   % (nm, s["n"], nm, cw, nm, ch, nm, r - b))
        # THE SIZE BAND, and it is the cube's and not the sprite's. See the header.
        if not (150 <= s["n"] <= 520):
            ok = False
        if not (12 <= cw <= 24):
            ok = False
        if not (14 <= ch <= 28):
            ok = False

    # THE COLOUR LEGS. Steel is neutral (the 1995 sprite is blue), wood is olive.
    if means[0] is not None:
        r, g, b = means[0]
        if abs(r - b) > 10 or abs(g - b) > 10:
            ok = False
    if means[1] is not None:
        r, g, b = means[1]
        if (r - b) < 25 or (g - b) < 20:
            ok = False
    # AND THEY MUST BE TWO DIFFERENT MESHES. One cube bound to both kinds passes every
    # leg above except this one.
    if means[0] is not None and means[1] is not None:
        d = sum(abs(means[0][k] - means[1][k]) for k in range(3))
        out.append("kinddiff=%d" % d)
        if d < 40:
            ok = False
    else:
        out.append("kinddiff=0")

    out.append("verdict=%s" % ("cube" if ok else "not-cube"))
    print("|".join(out))
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1], sys.argv[2]))
