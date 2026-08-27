"""Fit a faction emblem into the radar surface.

The radar opening is 136x120 (aspect 1.133) and the emblems arrive square at ~1254px.
The fit is CROP THEN REDUCE, never squash: the source is centre-cropped to 1.133 first, so
the shield keeps its proportions, and only then area-averaged down. The emblem art carries
its own dark plate texture right out to the edges, so cropping the top and bottom costs
background, not subject.

Area averaging (PIL BOX) is the right filter at a ~9x reduction. LANCZOS rings on the
hard metal edges of the shield bevel and leaves halos that read as noise once the image
is only 136px wide.

The chassis draws the radar opening as OPAQUE dark, not a hole, so the emblem is written
as flat RGB with no alpha and simply covers the opening.
"""
from PIL import Image
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
CH = os.path.join(HERE, "chunks")
IN = os.path.join(HERE, "incoming")

RADAR_W, RADAR_H = 136, 120

SOURCES = {
    "radar_off_nod": "nod_minimap_logo.png",
    # GDI was fitted the same way in an earlier pass; kept here so both are reproducible.
    "radar_off_gdi": "gdi_minimap_logo.png",
}


def fit(src):
    im = Image.open(src).convert("RGB")
    want = RADAR_W / float(RADAR_H)
    have = im.width / float(im.height)
    if have > want:                      # too wide: trim the sides
        w = int(round(im.height * want))
        x = (im.width - w) // 2
        im = im.crop((x, 0, x + w, im.height))
    elif have < want:                    # too tall: trim top and bottom
        h = int(round(im.width / want))
        y = (im.height - h) // 2
        im = im.crop((0, y, im.width, y + h))
    return im.resize((RADAR_W, RADAR_H), Image.BOX)


def build():
    made = []
    for name, fn in SOURCES.items():
        p = os.path.join(IN, fn)
        if not os.path.exists(p):
            print("skip %-16s (no %s)" % (name, fn))
            continue
        out = os.path.join(CH, name + ".png")
        src = Image.open(p)
        fit(p).save(out)
        print("%-16s %dx%d -> %dx%d" % (name, src.width, src.height, RADAR_W, RADAR_H))
        made.append(name)
    if not made:
        sys.exit("nothing to do")


if __name__ == "__main__":
    build()
