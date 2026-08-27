#!/usr/bin/env python3
"""Pixel arithmetic for G36, the Tier 2 presentation gate.

Every number this prints is a count of PIXELS, measured off shots the gate just took.
Nothing here reads an exit code, which is the first trap worth naming: a chain
that silently did nothing would leave every program returning 0.

  diff   A B          -> DIFF|<changed>|<darker>|<brighter>
  strip  A B N        -> STRIP|<changed in the rightmost N columns>
  key    A B          -> KEY|<magenta-family pixels in B>
  black  A B N        -> BLACK|<pure black in A>|<in B>, left of the last N columns
"""
import sys
from PIL import Image


def load(p):
    im = Image.open(p).convert("RGB")
    return im.size, list(im.getdata())


def main():
    what = sys.argv[1]
    (wa, ha), a = load(sys.argv[2])
    (wb, hb), b = load(sys.argv[3])
    if (wa, ha) != (wb, hb):
        print("SIZE|mismatch|%dx%d|%dx%d" % (wa, ha, wb, hb))
        return 1

    if what == "diff":
        changed = darker = brighter = 0
        for x, y in zip(a, b):
            if x == y:
                continue
            changed += 1
            if sum(y) < sum(x):
                darker += 1
            else:
                brighter += 1
        print("DIFF|%d|%d|%d" % (changed, darker, brighter))
        return 0

    if what == "rect":
        # Everything that changed inside one rectangle, and which way. Used for the new
        # HUD's radar surface and for a build cell's border ring.
        x0, y0, x1, y1 = (int(v) for v in sys.argv[4:8])
        changed = 0
        for row in range(max(0, y0), min(ha, y1)):
            base = row * wa
            for col in range(max(0, x0), min(wa, x1)):
                if a[base + col] != b[base + col]:
                    changed += 1
        print("RECT|%d" % changed)
        return 0

    if what == "ring":
        # The 1px ring just inside a rectangle: the build cell's own frame. With the
        # cameo drawn over the bevel these pixels are picture; with the frame put back on
        # top they are the chassis's dark metal. "Frame-like" is low saturation AND dark,
        # which no C&C95 cameo edge is for a whole ring.
        x0, y0, x1, y1 = (int(v) for v in sys.argv[4:8])
        def framey(px):
            r, g, bl = px
            return max(r, g, bl) < 110 and (max(r, g, bl) - min(r, g, bl)) < 40
        na = nb = tot = 0
        for row in range(max(0, y0), min(ha, y1)):
            for col in range(max(0, x0), min(wa, x1)):
                edge = (row == y0 or row == y1 - 1 or col == x0 or col == x1 - 1)
                if not edge:
                    continue
                tot += 1
                if framey(a[row * wa + col]):
                    na += 1
                if framey(b[row * wa + col]):
                    nb += 1
        print("RING|%d|%d|%d" % (tot, na, nb))
        return 0

    if what == "chamfer":
        # The four 45-degree corner cuts of one build cell. Args are the cell rect in
        # screen pixels plus the cut size, already multiplied by the bar's zoom. A pixel
        # is in a cut when its distance from the corner, measured along the two axes
        # together, is less than that. Before the cut those pixels are cameo; after it
        # they are the chassis showing through, which is dark and unsaturated.
        x0, y0, x1, y1, cut = (int(v) for v in sys.argv[4:9])
        w = x1 - x0
        h = y1 - y0
        def framey(px):
            r, g, bl = px
            return max(r, g, bl) < 110 and (max(r, g, bl) - min(r, g, bl)) < 40
        na = nb = tot = 0
        for row in range(max(0, y0), min(ha, y1)):
            cy = row - y0
            for col in range(max(0, x0), min(wa, x1)):
                cx = col - x0
                incut = (cx + cy < cut or (w - 1 - cx) + cy < cut or
                         cx + (h - 1 - cy) < cut or (w - 1 - cx) + (h - 1 - cy) < cut)
                if not incut:
                    continue
                tot += 1
                if framey(a[row * wa + col]):
                    na += 1
                if framey(b[row * wa + col]):
                    nb += 1
        print("CHAMFER|%d|%d|%d" % (tot, na, nb))
        return 0

    if what == "bilfix":
        # A is the frame rendered with --nobilfix, B with the corrections on. Returns
        # both halves of the defect at once, so the gate compares two real renders
        # rather than testing a threshold somebody guessed:
        #   darker  pixels the UNFIXED frame draws markedly darker than the fixed one.
        #           That is the terrain hairline: a bilinear sample on an atlas cell's
        #           edge blending the neighbouring tile.
        #   keyA/keyB  magenta-family pixels in each. The DOS transparency key is
        #           (255,0,255) and nothing on this battlefield is legitimately pink.
        #
        # The key floor is an optional fifth argument because the fringe's BRIGHTNESS is
        # not a property of the fringe. It is a decal edge, and once the decal pass began
        # carrying the terrain's own per-corner light the same fringe arrived at the
        # framebuffer multiplied by about 0.63, which took the unfixed frame from 423
        # pixels over a floor of 110 to 19 without a single fringe pixel disappearing:
        # the brightest one only moved from (168,55,160) to (141,46,135). A caller whose
        # subject is lit passes a floor scaled the same way. Below about 55 the test stops
        # discriminating at all, because the water and the shadows start answering to it.
        cut = int(sys.argv[4])
        floor = int(sys.argv[5]) if len(sys.argv) > 5 else 110
        darker = keyA = keyB = 0
        for row in range(ha):
            base = row * wa
            for col in range(0, wa - cut):
                i = base + col
                r, g, bl = a[i]
                if r > floor and bl > floor and g < 70 and abs(r - bl) < 70:
                    keyA += 1
                r2, g2, b2 = b[i]
                if r2 > floor and b2 > floor and g2 < 70 and abs(r2 - b2) < 70:
                    keyB += 1
                if sum(a[i]) + 45 < sum(b[i]):
                    darker += 1
        print("BILFIX|%d|%d|%d" % (darker, keyA, keyB))
        return 0

    if what == "key":
        # Key-colour fringe. The DOS art's transparency key is MAGENTA (255,0,255) and
        # smudge.pack carries it behind 9827 transparent texels; bilinear filtering
        # averages it into every decal edge unless the RGB has been bled outwards
        # first. Anything in the magenta family here is that fringe, because nothing in
        # a Tiberian Dawn battlefield is legitimately bright pink.
        n = 0
        for r, g, b in b:
            if r > 110 and b > 110 and g < 70 and abs(r - b) < 70:
                n += 1
        print("KEY|%d" % n)
        return 0

    if what == "black":
        # Pure black inside the tactical view only, both images. A bilinear sample
        # taken on an atlas cell's edge blends in the neighbouring tile; where that is
        # one of the atlas's alpha-0 water holes the alpha test discards the fragment
        # and the clear colour shows through as a hairline. Counting black is how that
        # hairline is measured without knowing where the cell edges fall.
        n = int(sys.argv[4])
        ka = kb = 0
        for row in range(ha):
            base = row * wa
            for col in range(0, wa - n):
                if a[base + col] == (0, 0, 0):
                    ka += 1
                if b[base + col] == (0, 0, 0):
                    kb += 1
        print("BLACK|%d|%d" % (ka, kb))
        return 0

    if what == "strip":
        # The rightmost N columns: the DOS sidebar, drawn at 1:1 AFTER the chain has
        # resolved. If one pixel of it moves, the seam is in the wrong place.
        n = int(sys.argv[4])
        changed = 0
        for row in range(ha):
            base = row * wa
            for col in range(wa - n, wa):
                if a[base + col] != b[base + col]:
                    changed += 1
        print("STRIP|%d" % changed)
        return 0

    print("usage: gate_gfx.py diff|strip|key|black A B [N]")
    return 2


if __name__ == "__main__":
    sys.exit(main())
