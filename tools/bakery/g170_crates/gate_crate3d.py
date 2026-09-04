#!/usr/bin/env python3
"""G170: the bonus crates are the CARTRIDGE'S 3D CUBE and not the 1995 flat sprite.

    python3 gate_crate3d.py ON.png OFF.png

Both shots are the SAME frame of SCG32EA at cam 7.0 60.0, zoom max, tick 60. ON draws
the crate pass, OFF has it switched out with the script's `crates 0`. Every number below
is the difference between them, so nothing in the frame except the crate pass can move
one of them, and a pass that drew nothing at all would report zero rather than pass.

WHY THAT MISSION AND THAT FRAME. SCG32EA is the only kind of map that can prove BOTH
bindings in one picture: its [OVERLAY] section places cell 3782 = SCRATE and cell
3783 = WCRATE, which is a steel crate at (6,59) with a wooden one immediately east of it.
So the left cluster in the frame must be the steel cube and the right cluster the wooden
one, and a renderer that bound one mesh to both kinds fails on colour rather than on size.

WHAT SEPARATES A CUBE FROM A SPRITE, measured off the ROM and off both renders:
  the cartridge's cube is 185 x 185 x 184 mesh units, 0.18 of a cell on a side, and it
  subtends 16 x 21 screen pixels at this camera, 313 changed pixels per crate.
  the 1995 sprite is 10 x 11 of a 24-pixel DOS cell, 0.42 of a cell, and it subtends
  34 x 25 pixels, 646 changed pixels per crate.
The width band below is 12..24, which the sprite's 34 cannot enter. That is the leg the
mutation test exercises: run the same gate against a pack baked before the crate entries
and the fallback sprites draw, 34 pixels wide, and this reddens.

AND THE SECOND, INDEPENDENT LEG IS COLOUR. The cartridge's steel cube is neutral grey
(measured mean (83,82,80), r-b = +3), while the 1995 steel sprite is blue-grey
(mean (94,94,114), r-b = -20). So `steel_rb` inside a small band around zero is a second
witness that the cube is what is on the glass, and it is a witness the size leg cannot
fake. The wooden crate is olive in both, which is why its colour leg only proves the two
kinds were not swapped.
"""
import sys

try:
    from PIL import Image
except ImportError:
    print("gate_crate3d: PIL is not installed")
    sys.exit(2)

# The two crates sit either side of this column in the gate's frame. They are 65 pixels
# apart and 16 wide, so any split between them serves; this one is the midpoint.
SPLIT_X = 640
# A pixel counts as changed when its three channels differ by this much in total. Well
# under the crate's own contrast against grass and well over the shot's own dither.
THRESH = 12


def main(on_path, off_path):
    on = Image.open(on_path).convert("RGB")
    off = Image.open(off_path).convert("RGB")
    if on.size != off.size:
        print("gate_crate3d: the two shots are different sizes, %s vs %s"
              % (on.size, off.size))
        return 2
    w, h = on.size
    pon, poff = on.load(), off.load()

    # left cluster, right cluster, and everything else. "Everything else" must be empty:
    # a crate pass that touched the rest of the frame would be a state leak, and this is
    # the only place it would ever be caught.
    stats = [dict(n=0, x0=w, x1=-1, y0=h, y1=-1, r=0, g=0, b=0) for _ in range(2)]
    stray = 0
    for y in range(h):
        for x in range(w):
            a, o = pon[x, y], poff[x, y]
            if abs(a[0] - o[0]) + abs(a[1] - o[1]) + abs(a[2] - o[2]) <= THRESH:
                continue
            s = stats[0] if x < SPLIT_X else stats[1]
            # A change far from either crate cell is a stray; the windows are generous
            # (48 px) so a correct crate can grow or shift without tripping it.
            if not (280 <= y <= 380 and (570 <= x <= 630 or 650 <= x <= 710)):
                stray += 1
                continue
            s["n"] += 1
            s["x0"] = min(s["x0"], x); s["x1"] = max(s["x1"], x)
            s["y0"] = min(s["y0"], y); s["y1"] = max(s["y1"], y)
            s["r"] += a[0]; s["g"] += a[1]; s["b"] += a[2]

    out = ["stray=%d" % stray]
    ok = (stray == 0)
    names = ("steel", "wood")
    means = []
    for i, s in enumerate(stats):
        nm = names[i]
        if s["n"] == 0:
            out.append("%s_n=0" % nm)
            ok = False
            means.append(None)
            continue
        cw = s["x1"] - s["x0"] + 1
        ch = s["y1"] - s["y0"] + 1
        r = s["r"] // s["n"]; g = s["g"] // s["n"]; b = s["b"] // s["n"]
        means.append((r, g, b))
        out.append("%s_n=%d %s_w=%d %s_h=%d %s_rb=%d %s_gb=%d"
                   % (nm, s["n"], nm, cw, nm, ch, nm, r - b, nm, g - b))
        # THE SIZE BAND, and it is the cube's and not the sprite's. See the header.
        if not (150 <= s["n"] <= 520):
            ok = False
        if not (12 <= cw <= 24):
            ok = False
        if not (14 <= ch <= 28):
            ok = False

    # THE COLOUR LEGS. Steel is neutral (the sprite is blue), wood is olive.
    if means[0] is not None:
        r, g, b = means[0]
        if abs(r - b) > 10 or abs(g - b) > 10:
            ok = False
    if means[1] is not None:
        r, g, b = means[1]
        if (r - b) < 25 or (g - b) < 20:
            ok = False
    # AND THEY MUST BE TWO DIFFERENT MESHES. One cube bound to both kinds would pass
    # every leg above except this one.
    if means[0] is not None and means[1] is not None:
        d = sum(abs(means[0][k] - means[1][k]) for k in range(3))
        out.append("kinddiff=%d" % d)
        if d < 40:
            ok = False
    else:
        out.append("kinddiff=0")

    out.append("verdict=%s" % ("cube" if ok else "not-cube"))
    print(" ".join(out))
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1], sys.argv[2]))
