import sys
from PIL import Image, ImageChops

# A dynamic light must FADE when its source ends, not stop.
#
# Reported: a light alongside a muzzle flash vanished the instant the flash ended.
# It should fade out rather than disappear.
#
# Measured as lit pixels per frame against a lights-off run of the same scene, which is
# the only way to separate the light from everything else that moves. The gate wants two
# things a count alone cannot give: that the light SURVIVES its source, and that it is
# strictly falling while it does. A light that lingered at constant brightness would pass
# a "still lit" test and be just as wrong as one that snaps off.
base = sys.argv[1]          # lights OFF reference frames, %02d
lit  = sys.argv[2]          # lights ON frames, %02d
n    = int(sys.argv[3])
vals = []
for i in range(n):
    try:
        a = Image.open(base % i).convert("RGB").crop((0, 0, 1000, 720))
        b = Image.open(lit % i).convert("RGB").crop((0, 0, 1000, 720))
    except Exception:
        vals.append(0); continue
    d = ImageChops.difference(a, b).convert("L").point(lambda v: 255 if v > 4 else 0)
    vals.append(sum(1 for p in d.getdata() if p))
peak = max(vals) if vals else 0
pi = vals.index(peak) if peak else 0
tail = vals[pi + 1:]
# the run of strictly-falling frames after the peak, before it reaches zero
steps = 0
for j in range(len(tail)):
    if tail[j] <= 0:
        break
    if j and tail[j] >= tail[j - 1]:
        break
    steps += 1
print("LIGHTFADE|peak=%d|decaysteps=%d" % (peak, steps))
