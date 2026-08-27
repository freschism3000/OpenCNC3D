#!/usr/bin/env python3
"""Rebuild C&C3D.app's icon from the cartridge's own art.

    python3 tools/launchers/make_icon.py

WHY THIS IS A SCRIPT AND NOT A CHECKED-IN PNG. The .icns is a build output made
of ten rescalings of one source, and the source is art that lives in the repo
already. A committed binary would drift from it silently the first time the
emblem is re-cut.

THE SOURCE is the GDI eagle medallion the 640x480 HUD puts in its radar well,
tools/sidebar_redesign/gdi_eagle/02_isolated_80x69.png. Two things are done to
it and both are deliberate:

  * MASKED TO ITS CIRCLE. The "isolated" cut leaves grey fringe from the plate it
    was lifted off, and at icon size that reads as dirt around the medallion. The
    emblem is a circle, so everything outside the largest inscribed ellipse of the
    opaque region is dropped. That removes the fringe without repainting any of
    the art itself.
  * UPSCALED WITH NEAREST, at a whole-number factor. This is a pixel-art game
    rendered from a 1996 cartridge; a smooth resample would make the icon look
    like a blurred photograph of the game rather than the game.

The plate behind it is the HUD's own panel tone (the mean colour of
art_new/panel_body.png) as a vertical gradient, so the icon and the sidebar agree.
"""
import os
import subprocess
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
EAGLE = os.path.join(REPO, "tools/sidebar_redesign/gdi_eagle/02_isolated_80x69.png")
PANEL = os.path.join(REPO, "tools/sidebar_redesign/art_new/panel_body.png")
APP = os.path.join(HERE, "C&C3D.app")
S = 1024


def cleaned_eagle():
    """The medallion with the isolation fringe removed, cropped to its bounds."""
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


def compose():
    base = tuple(int(x) for x in
                 np.array(Image.open(PANEL).convert("RGB")).reshape(-1, 3).mean(0))
    top = tuple(min(255, int(c * 2.6)) for c in base)
    bot = tuple(int(c * 0.5) for c in base)

    grad = Image.new("RGB", (1, S))
    for y in range(S):
        t = y / (S - 1)
        grad.putpixel((0, y), tuple(int(top[k] * (1 - t) + bot[k] * t) for k in range(3)))
    grad = grad.resize((S, S))

    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).rounded_rectangle([36, 36, S - 36, S - 36], radius=190, fill=255)
    img.paste(grad, (0, 0), mask)
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([36, 36, S - 36, S - 36], radius=190,
                        outline=(198, 186, 150, 200), width=7)
    d.rounded_rectangle([50, 50, S - 50, S - 50], radius=178, outline=(0, 0, 0, 80), width=3)

    eagle = cleaned_eagle()
    e = eagle.resize((eagle.width * 7, eagle.height * 7), Image.NEAREST)
    at = ((S - e.width) // 2, (S - e.height) // 2)
    shadow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    shadow.paste(Image.new("RGBA", e.size, (0, 0, 0, 150)), (at[0] + 10, at[1] + 16), e)
    img = Image.alpha_composite(img, shadow.filter(ImageFilter.GaussianBlur(14)))
    img.paste(e, at, e)
    return img


def main():
    img = compose()
    iconset = "/tmp/cnc3d.iconset"
    os.makedirs(iconset, exist_ok=True)
    for sz in (16, 32, 64, 128, 256, 512, 1024):
        img.resize((sz, sz), Image.LANCZOS).save("%s/icon_%dx%d.png" % (iconset, sz, sz))
        if sz <= 512:
            img.resize((sz * 2, sz * 2), Image.LANCZOS).save(
                "%s/icon_%dx%d@2x.png" % (iconset, sz, sz))
    out = os.path.join(APP, "Contents/Resources/AppIcon.icns")
    subprocess.check_call(["iconutil", "-c", "icns", iconset, "-o", out])
    print("wrote %s" % out)

    # THE WINDOWS HALF OF THE SAME ICON, so the executable is recognisable there too
    # executable per platform with a game icon on it. Same composed image, so the two
    # platforms cannot drift into different emblems; only the container differs.
    # PIL writes a multi-size .ico from one call, and 256 is the largest Explorer uses.
    ico = os.path.join(os.path.dirname(APP), "cnc3d.ico")
    img.save(ico, sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64),
                         (128, 128), (256, 256)])
    print("wrote %s" % ico)
    print("now:  cp -R \"%s\" <repo>/playable/" % APP)


if __name__ == "__main__":
    sys.exit(main())
