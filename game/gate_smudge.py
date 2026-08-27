import sys
from PIL import Image, ImageChops
off = Image.open(sys.argv[1]).convert("RGB")
on  = Image.open(sys.argv[2]).convert("RGB")
d = ImageChops.difference(off, on).convert("L").point(lambda v: 255 if v > 12 else 0)
n = sum(1 for p in d.getdata() if p)

# WHAT MAKES THIS MORE THAN A COUNT. A decal replaces GRASS with EARTH, so the changed
# pixels must lose GREEN DOMINANCE -- the green share of the pixel's own energy has to
# fall. That is true of all three kinds at once and is what a count alone cannot see:
# a blend gone wrong, lighting the scene uniformly, would keep the green share exactly
# where it was.
#
# The first version of this gate asserted the pixels got DARKER, and it was WRONG and
# said so on its first run: measured over a real frame the decals take grass from
# (32,61,27) to (95,81,62), which is BRIGHTER. A dirt apron is lighter than grass; only
# a scorch is darker. The green share, however, falls on 98.5% of them.
o, m = off.load(), on.load()
w, h = off.size
browner = 0
for y in range(h):
    for x in range(w):
        if not d.getpixel((x, y)):
            continue
        a, b = o[x, y], m[x, y]
        if b[1] / max(1, sum(b)) < a[1] / max(1, sum(a)):
            browner += 1
print("SMUDGE|changed=%d|browner=%d" % (n, browner))
