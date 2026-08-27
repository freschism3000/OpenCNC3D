#!/usr/bin/env python3
"""Build the Editor app's icon.

    python3 tools/launchers/make_editor_icon.py

SAME EAGLE, DIFFERENT PLATE. The icon has one job beyond looking right: telling
Editor apart from C&C3D at Dock size, where both are a 32-pixel square. So it
reuses make_icon.py's medallion exactly -- two emblems that drifted apart would
be worse than one -- and changes everything around it:

  * a blueprint-blue plate instead of the HUD's warm panel tone, which is the
    difference that survives being shrunk to 32 pixels;
  * a drafting grid across it, at the same 45-degree lean the game draws its
    ground on, so the plate reads as a map being worked on;
  * an amber corner tab, matching the SAVE button in the editor's own panel.

The grid is drawn as full lines and then masked to the plate, rather than
clipped line by line: at icon sizes a line that stops one pixel short of the
border reads as a mistake.
"""
import os
import subprocess
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
EAGLE = os.path.join(REPO, "tools/sidebar_redesign/gdi_eagle/02_isolated_80x69.png")
APP = os.path.join(HERE, "Editor.app")
S = 1024

PLATE_TOP = (34, 52, 74)        # blueprint blue, lit from above
PLATE_BOT = (12, 18, 27)        # the editor panel's own ground
GRID = (86, 132, 168, 60)
RIM = (198, 186, 150, 200)
AMBER = (224, 176, 112, 255)


def cleaned_eagle():
    """The medallion with the isolation fringe removed, cropped to its bounds.

    Identical to make_icon.py: the grey fringe left by the isolation cut reads as
    dirt at icon size, and the emblem is a circle, so everything outside the largest
    inscribed ellipse of the opaque region is dropped. No art is repainted.
    """
    a = np.array(Image.open(EAGLE).convert("RGBA"))
    opaque = a[:, :, 3] > 200
    ys, xs = np.nonzero(opaque)
    assert len(xs), "the eagle source has no opaque pixels"
    cx, cy = (xs.min() + xs.max()) / 2.0, (ys.min() + ys.max()) / 2.0
    rx, ry = (xs.max() - xs.min()) / 2.0, (ys.max() - ys.min()) / 2.0
    Y, X = np.mgrid[0:a.shape[0], 0:a.shape[1]]
    inside = ((X - cx) / (rx * 0.93)) ** 2 + ((Y - cy) / (ry * 0.93)) ** 2 <= 1.0
    a[:, :, 3] = np.where(inside & opaque, 255, 0).astype(np.uint8)
    im = Image.fromarray(a)
    return im.crop(im.getbbox())


def plate():
    grad = Image.new("RGB", (1, S))
    for y in range(S):
        t = y / (S - 1)
        grad.putpixel((0, y), tuple(int(PLATE_TOP[k] * (1 - t) + PLATE_BOT[k] * t)
                                    for k in range(3)))
    return grad.resize((S, S))


def drafting_grid():
    """Isometric rules at the lean the game draws its ground on."""
    g = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(g)
    step = 78
    for i in range(-S, S * 2, step):
        d.line([(i, 0), (i + S, S)], fill=GRID, width=4)
        d.line([(i, S), (i + S, 0)], fill=GRID, width=4)
    return g


def compose():
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).rounded_rectangle([36, 36, S - 36, S - 36], radius=190, fill=255)
    img.paste(plate(), (0, 0), mask)
    # Full-bleed grid, masked to the plate afterwards -- see the module note.
    img.paste(drafting_grid(), (0, 0), Image.composite(
        drafting_grid().split()[3], Image.new("L", (S, S), 0), mask))

    d = ImageDraw.Draw(img)
    d.rounded_rectangle([36, 36, S - 36, S - 36], radius=190, outline=RIM, width=7)
    d.rounded_rectangle([50, 50, S - 50, S - 50], radius=178,
                        outline=(0, 0, 0, 80), width=3)

    eagle = cleaned_eagle()
    e = eagle.resize((eagle.width * 7, eagle.height * 7), Image.NEAREST)
    at = ((S - e.width) // 2, (S - e.height) // 2 - 18)
    shadow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    shadow.paste(Image.new("RGBA", e.size, (0, 0, 0, 170)), (at[0] + 10, at[1] + 16), e)
    img = Image.alpha_composite(img, shadow.filter(ImageFilter.GaussianBlur(14)))
    img.paste(e, at, e)

    # The amber bar: the editor panel's SAVE button, and the one warm thing on a cold
    # plate. At 32 pixels it is the tell.
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([250, S - 210, S - 250, S - 148], radius=26, fill=AMBER)
    return img


def main():
    img = compose()
    iconset = "/tmp/cnc3d-editor.iconset"
    os.makedirs(iconset, exist_ok=True)
    for sz in (16, 32, 64, 128, 256, 512, 1024):
        img.resize((sz, sz), Image.LANCZOS).save("%s/icon_%dx%d.png" % (iconset, sz, sz))
        if sz <= 512:
            img.resize((sz * 2, sz * 2), Image.LANCZOS).save(
                "%s/icon_%dx%d@2x.png" % (iconset, sz, sz))
    out = os.path.join(APP, "Contents/Resources/AppIcon.icns")
    subprocess.check_call(["iconutil", "-c", "icns", iconset, "-o", out])
    img.resize((512, 512), Image.LANCZOS).save("/tmp/editor-icon-preview.png")
    print("wrote %s" % out)
    print("preview /tmp/editor-icon-preview.png")


if __name__ == "__main__":
    sys.exit(main())
