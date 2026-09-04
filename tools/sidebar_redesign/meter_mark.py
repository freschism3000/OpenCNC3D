"""Draw the power meter's DRAIN MARKER for the 640x480 HUD -> chunks/meter_drain_mark.png.

The meter's green fill says how much power the base MAKES. On its own that cannot tell a
base with two idle plants apart from one that is browning out, so the channel also needs a
mark saying how much of it is already spoken for. The DOS gauge has always had one: an 8x3
POWER shape drawn across the full width of the bar at the drain height. This is that mark
at the art HUD's scale, so the two sidebars mark one level with one mark rather than each
inventing a way to show it.

THE SILHOUETTE AND THE TWO COLOURS ARE THE DOS SHAPE'S OWN. Read them back out of the
sidebar pack with `python3 tools/dosbar_art.py export playable/dossidebar.pack <dir>`:

    L L . . . . L L        L = palette index 10, RGB (80, 80, 252)
    D D L L L L D D        D = palette index 11, RGB (0, 0, 168)
    . . D D D D . .        . = index 0, transparent

A light bracket at each end, a light bar through the middle, a dark shadow under the bar.

WHAT IS OURS, and it is two things:

  * THE SIZE. The DOS bar is 8 pixels wide and this channel is 17, which is not a whole
    multiple, so a nearest-neighbour scale puts the odd column on one side and lands a
    symmetric mark lopsided. It is redrawn on the 17 wide lattice instead: a quarter of
    the width as bracket at each end, the rest as bar. The height is 5 and it is ODD on
    purpose, because hud640.c hangs the mark on its centre row (`mark->h / 2`); an even
    height would put the reading half a pixel off the level it is reporting.
  * KEEPING IT BLUE. Blue is the one hue this meter's fill never takes: the fill is the
    delivered green rotated towards amber and then red as the base overdraws. So the mark
    reads the same in all three states, which is exactly when it is most needed.

The result is committed like every other chunk, so a bake never depends on this having
been run. It is here so the mark is reproducible and its provenance is not folk memory.
"""
from PIL import Image
import json, os

HERE = os.path.dirname(os.path.abspath(__file__))
CH   = os.path.join(HERE, "chunks")
OUT  = os.path.join(CH, "meter_drain_mark.png")

LT    = (80, 80, 252, 255)      # DOS palette index 10
DK    = (0,  0,  168, 255)      # DOS palette index 11
CLEAR = (0,  0,  0,   0)

HEIGHT = 5                      # odd, so the middle row IS the reading


def build():
    # The width comes from layout.json rather than a literal, for the reason hud.py reads
    # it too: the channel's width and this mark's width are one number, and two copies of
    # a number drift.
    w = json.load(open(os.path.join(CH, "layout.json")))["METER"][2]
    bracket = w // 4            # the DOS mark gives 2 of its 8 columns to each bracket
    mid = HEIGHT // 2
    im = Image.new("RGBA", (w, HEIGHT), CLEAR)
    px = im.load()
    for x in range(w):
        end = x < bracket or x >= w - bracket
        for y in range(HEIGHT):
            if end:
                px[x, y] = LT if y < mid else (DK if y == mid else CLEAR)
            else:
                px[x, y] = LT if y == mid else (DK if y > mid else CLEAR)
    im.save(OUT)
    print("meter_drain_mark  %dx%d  bracket %d  -> %s" % (w, HEIGHT, bracket, OUT))


if __name__ == "__main__":
    build()
