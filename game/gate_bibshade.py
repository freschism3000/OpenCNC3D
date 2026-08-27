"""G83: the decal pass carries the terrain's own per-corner light.

argv: <no-smudge.png> <smudge, shade on.png> <smudge, --noshade.png>

Three claims, because none of them alone can tell the truth:

  ratio    the decal's mean luminance over the mean luminance of the GROUND BESIDE IT
           (the ring of pixels within 3 px of the decal, read from the no-smudge shot).
           A decal that ignores the terrain light sits at full texel brightness over
           ground lit to 160/255 and this reads about 1.5. A decal that is modulated
           twice reads about 0.66. It has to land near 1.

  shade    the per-pixel quotient of the lit decal over the SAME decal rendered with
           --noshade. Under GL_MODULATE that quotient is exactly the primary colour the
           pass emitted, whatever the art underneath is, so it measures the shade itself
           and not the picture. Its mean must be the ground's own shade.

  spread   that quotient must VARY across the frame. The bibs in this view straddle two
           lighting bands (corner lit=160 in the north, lit=183 in the south), so a pass
           that emitted one constant colour -- including the right average one -- gives a
           spread of zero and fails here even though the first two claims would pass.

The mask is eroded by 2 px before the quotient is taken, so antialiased decal edges,
where the pixel is part decal and part ground, do not enter the average.
"""
import sys
from PIL import Image, ImageChops, ImageFilter

none = Image.open(sys.argv[1]).convert("RGB")
lit = Image.open(sys.argv[2]).convert("RGB")
nolit = Image.open(sys.argv[3]).convert("RGB")
if not (none.size == lit.size == nolit.size):
    print("BIBSHADE|error=size mismatch")
    sys.exit(1)

w, h = none.size
mask = ImageChops.difference(none, lit).convert("L").point(lambda v: 255 if v > 12 else 0)
ring = mask.filter(ImageFilter.MaxFilter(7))
core = mask.filter(ImageFilter.MinFilter(5))

m, r, c = mask.load(), ring.load(), core.load()
pn, pl, pu = none.load(), lit.load(), nolit.load()


def lum(p):
    return 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2]


ndecal = nring = ncore = 0
sdecal = sring = 0.0
qsum = 0.0
qmin, qmax = 9.9, 0.0
for y in range(h):
    for x in range(w):
        if m[x, y]:
            ndecal += 1
            sdecal += lum(pl[x, y])
            if c[x, y]:
                base = lum(pu[x, y])
                if base < 24.0:          # too dark to divide meaningfully
                    continue
                q = lum(pl[x, y]) / base
                ncore += 1
                qsum += q
                if q < qmin:
                    qmin = q
                if q > qmax:
                    qmax = q
        elif r[x, y]:
            nring += 1
            sring += lum(pn[x, y])

if ndecal == 0 or nring == 0 or ncore == 0:
    print("BIBSHADE|decalpx=%d|ringpx=%d|corepx=%d|error=nothing to measure"
          % (ndecal, nring, ncore))
    sys.exit(1)

ratio = (sdecal / ndecal) / (sring / nring)
qmean = qsum / ncore
spread = qmax - qmin
# The three r1000/s1000/p1000 fields are the same numbers as integer thousandths, because
# /bin/sh has no floating point and a shell that had to sed the decimals apart would end
# up comparing strings like "0703", which some `test` implementations read as octal.
print("BIBSHADE|decalpx=%d|corepx=%d|ratio=%.4f|shade=%.4f|spread=%.4f"
      "|r1000=%d|s1000=%d|p1000=%d"
      % (ndecal, ncore, ratio, qmean, spread,
         round(ratio * 1000), round(qmean * 1000), round(spread * 1000)))
