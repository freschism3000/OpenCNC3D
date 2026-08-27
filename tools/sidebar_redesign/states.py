"""Hover / pressed / active frames for the sidebar controls.

WHY THIS FILE REPLACES frames.py's PLACEHOLDERS
-----------------------------------------------
The first pass made states by multiplying the whole plate by a constant (0.62 for
"pressed") and tinting it red for "on". That is an overlay wearing a sprite's clothes:
every facet moves the same way, so the plate reads as a flat rectangle with a filter
over it rather than as a piece of metal being lit or pushed in. This was called out
twice, and that is right.

Real bevelled metal states work on the SHAPE:

  hover   - the plate catches more light. The gain is NOT uniform and it is NOT largest
            on the top lip: that lip is already the brightest thing in the delivered art
            and has no headroom, so boosting it only clips it into a white smear and
            destroys the bevel ordering. The headroom lives in the dark face, so that is
            what hover lifts, with the side lips following and the top lip barely moving.

  pressed - the bevel INVERTS. What was the lit top lip becomes the shadowed inner wall
            of a recess, and the bottom lip catches the light instead. The face darkens
            and the engraved content shifts one pixel down-right, which is what your eye
            reads as "this went in".

  active  - the face is lit warm from the amber indicator ramp already present in the
            art (the LED strip under the arrows), so "engaged" reads as the panel's own
            language rather than an imported red.

FACET DERIVATION
----------------
No hand-typed row numbers. Each pixel inside the chamfer mask is assigned the boundary
it lies nearest (up / down / left / right) within a 3px band; everything deeper than
that is face. Lighting is then applied per facet class. Same idea as material.py, but
driven off the mask instead of a synthesised heightfield, because here the art already
exists and only needs re-lighting.

Frame order, and it is load-bearing - hud640.c and cnc_sidebar.h index these directly:
    0 normal   1 hover   2 pressed   3 active (buttons only)

Frame 0 is emitted into the strip for completeness but is NOT blitted at runtime; the
chassis already carries every control in its resting state.
"""
from PIL import Image
import numpy as np, os, json

HERE = os.path.dirname(os.path.abspath(__file__))
CH = os.path.join(HERE, "chunks")

# Plate outlines, measured in slice_ref.py's 160x480 space.
#
# chamfer is how much of each corner is NOT plate. Do not guess it - READ IT OFF THE ART.
# The plate's bright top bevel traces its own outline, so the chamfer is the Manhattan
# distance at which that bevel turns the corner. On button_repair the bevel sits at
# (1,2), (2,1), (3,0): distance 3, so chamfer is 3.
#
# It was 5 first, which carved two extra rings of real plate off every corner, and then
# 0, which squared the corners off. Both are invisible on the grey states and glaring on
# the gold one - too big leaves grey wedges, too small makes gold spill past the plate.
# The arrows really are octagons on a dark field, hence the much larger 7.
#
ELEMENTS = {
    "button_repair": dict(box=(6, 184, 56, 23), chamfer=3, active=True),
    "button_sell":   dict(box=(71, 184, 34, 23), chamfer=3, active=True),
    "button_map":    dict(box=(115, 184, 35, 23), chamfer=3, active=True),
    "arrow_up":      dict(box=(18, 453, 32, 25), chamfer=7, active=False),
    "arrow_down":    dict(box=(53, 453, 32, 25), chamfer=7, active=False),
}

BAND = 3                                 # bevel depth in pixels
AMBER = np.array([199.0, 154.0, 53.0])   # sampled from the existing LED strip

UP, DOWN, LEFT, RIGHT, FACE, OUTSIDE = 0, 1, 2, 3, 4, 5


def chamfer_mask(w, h, r):
    m = np.ones((h, w), bool)
    yy, xx = np.mgrid[0:h, 0:w]
    for (cx, cy) in ((0, 0), (w - 1, 0), (0, h - 1), (w - 1, h - 1)):
        m &= ~((np.abs(xx - cx) + np.abs(yy - cy)) < r)
    return m


def facets(mask):
    """Label each masked pixel by the boundary it is nearest, within BAND pixels."""
    h, w = mask.shape
    lab = np.where(mask, FACE, OUTSIDE).astype(np.uint8)
    big = 10 ** 6
    up = np.full((h, w), big, int)
    dn = np.full((h, w), big, int)
    lf = np.full((h, w), big, int)
    rt = np.full((h, w), big, int)
    for y in range(h):
        for x in range(w):
            if not mask[y, x]:
                continue
            d = 0
            while y - d - 1 >= 0 and mask[y - d - 1, x]:
                d += 1
            up[y, x] = d
            d = 0
            while y + d + 1 < h and mask[y + d + 1, x]:
                d += 1
            dn[y, x] = d
            d = 0
            while x - d - 1 >= 0 and mask[y, x - d - 1]:
                d += 1
            lf[y, x] = d
            d = 0
            while x + d + 1 < w and mask[y, x + d + 1]:
                d += 1
            rt[y, x] = d
    stack = np.stack([up, dn, lf, rt])
    near = stack.min(axis=0)
    which = stack.argmin(axis=0).astype(np.uint8)
    inband = mask & (near < BAND)
    lab[inband] = which[inband]
    return lab


def relight(base, mask, lab, gains, shift=(0, 0), warm=0.0, floor=0.0):
    """Apply a per-facet gain. `floor` lifts crushed shadows so the metal keeps its
    grain instead of going to flat black; `warm` blends the face toward AMBER at
    constant luminance."""
    a = np.array(base.convert("RGBA")).astype(float)
    rgb = a[..., :3]
    h, w = mask.shape

    g = np.ones((h, w), float)
    for cls, mul in gains.items():
        g[lab == cls] = mul
    out = rgb * g[..., None]

    # Soft knee, not a hard clip. A straight multiply drives already-bright pixels to
    # 255 across the whole lip, which erases the grain and turns the bevel into a white
    # smear. Compressing towards the ceiling keeps the ramp readable.
    knee = 168.0
    hi = out > knee
    out = np.where(hi, knee + (255.0 - knee) * (1.0 - np.exp(-(out - knee) / (255.0 - knee))), out)

    if floor:
        out = out + floor * (1.0 - out / 255.0) * 255.0 * 0.12

    if warm:
        # Warm EVERY facet by the same amount. Half-warming the top and left lips was
        # meant to keep them readable, but the amber blend already preserves the
        # luminance ramp, so all it actually did was leave a grey rim around a gold
        # button. Readability comes from the ramp, not from withholding the hue.
        lum = out @ np.array([0.299, 0.587, 0.114])
        tgt = AMBER / AMBER.mean()
        warmed = np.clip(lum[..., None] * tgt, 0, 255)
        out = out * (1 - warm) + warmed * warm

    out = np.clip(out, 0, 255)

    if shift != (0, 0):
        dy, dx = shift
        inner = (lab == FACE)
        rolled = np.roll(np.roll(out, dy, axis=0), dx, axis=1)
        out = np.where(inner[..., None], rolled, out)

    # The sprite is OPAQUE over its whole box, chamfer corners included, and those corners
    # carry the chassis pixels unchanged (OUTSIDE gets no gain). If the corners were left
    # transparent instead, the chassis's own resting bright edge would keep showing through
    # a pressed button and the plate would read as half-lit.
    out = np.where((lab == OUTSIDE)[..., None], rgb, out)

    res = np.zeros((h, w, 4), np.uint8)
    res[..., :3] = out.astype(np.uint8)
    res[..., 3] = 255
    return Image.fromarray(res, "RGBA")


def build():
    chassis = Image.open(f"{CH}/chassis.png").convert("RGBA")
    manifest = {}
    for name, spec in ELEMENTS.items():
        x, y, w, h = spec["box"]
        base = chassis.crop((x, y, x + w, y + h))
        mask = chamfer_mask(w, h, spec["chamfer"])
        lab = facets(mask)

        frames = [relight(base, mask, lab, {})]                      # 0 normal
        # 1 hover: see the note at the top on why UP is the SMALLEST gain here.
        frames.append(relight(base, mask, lab,
                              {UP: 1.22, LEFT: 1.55, FACE: 1.70, DOWN: 1.45, RIGHT: 1.45},
                              floor=0.7))
        # 2 pressed: bevel inverts and the face sinks one pixel down-right.
        frames.append(relight(base, mask, lab,
                              {UP: 0.30, LEFT: 0.44, FACE: 0.66, DOWN: 1.95, RIGHT: 1.45},
                              shift=(1, 1), floor=0.5))
        if spec["active"]:
            # 3 active: hover's lighting, carried into the panel's own amber ramp. Same
            # small gain on the top lip and for the same reason.
            frames.append(relight(base, mask, lab,
                                  {UP: 1.22, LEFT: 1.55, FACE: 1.70, DOWN: 1.45, RIGHT: 1.45},
                                  warm=0.85, floor=0.7))

        strip = Image.new("RGBA", (w * len(frames), h), (0, 0, 0, 0))
        for i, f in enumerate(frames):
            strip.alpha_composite(f, (i * w, 0))
        strip.save(f"{CH}/{name}.png")
        manifest[name] = dict(frames=len(frames), w=w, h=h, x=x, y=y)
        print("%-14s %d frames of %dx%d -> strip %dx%d"
              % (name, len(frames), w, h, strip.width, h))
    json.dump(manifest, open(f"{CH}/frames.json", "w"), indent=1)
    return manifest


if __name__ == "__main__":
    build()
