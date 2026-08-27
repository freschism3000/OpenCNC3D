import sys
from PIL import Image, ImageChops

# The Construction Yard's fans, asserted on PIXELS across consecutive frames.
#
# This gate exists because the fix for them reached testing THREE TIMES without working:
# twice as a wrong animation, and once as a correct one that never got into the build he
# launches, because the app binary was older than the renderer source and 35 of the 36
# mission packs had never been re-baked. A gate that reads the pack and the screen catches
# all three; a code review catches none of them.
#
# MEASURED IN A BOX, and the box is the point. The first version of this counted the whole
# frame and failed at 2427 against its own ceiling -- correctly, because the sidebar radar,
# the units and the effects all move too. Scoped to the fan plate at this script's fixed
# camera the answer is 340, and 340 is the fans.
BOX = (850, 300, 980, 420)
shots = [Image.open(p).convert("RGB").crop(BOX) for p in sys.argv[1:]]
acc = None
for i in range(1, len(shots)):
    d = ImageChops.difference(shots[0], shots[i]).convert("L").point(
        lambda v: 255 if v > 20 else 0)
    acc = d if acc is None else ImageChops.lighter(acc, d)
n = sum(1 for p in acc.getdata() if p)
print("FANS|changed=%d" % n)
