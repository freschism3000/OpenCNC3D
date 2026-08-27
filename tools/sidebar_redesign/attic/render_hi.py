from PIL import Image, ImageDraw, ImageFont
import sys, os, textwrap; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import kit as K, hires as X

OUT = "drafts_hi"; os.makedirs(OUT, exist_ok=True)
def font(b, s):
    p = "/System/Library/Fonts/Supplemental/Courier New%s.ttf" % (" Bold" if b else "")
    try: return ImageFont.truetype(p, s)
    except Exception: return ImageFont.load_default()
F, FS = font(True, 16), font(False, 12)

# the old bar shown at the same physical size for a fair comparison
old = K.upscale(K.assemble_original(), 2)
builds = [("ORIGINAL @2x", "", old, "old 80x200 bar at 2x: reaches only 400 of the 480 rows")]
for k, t in X.DESIGNS.items():
    b = X.build(t); b.save(f"{OUT}/{k}.png")
    builds.append((t["name"].upper(), k, b, t["blurb"]))

def sheet(out, S, Y0, Y1, cap, labels=True):
    GAP, TOP = 20, (104 if labels else 42)
    w, h = int(160*S), int((Y1-Y0)*S)
    c = Image.new("RGB", (len(builds)*(w+GAP)+GAP, TOP+h+30), (18,18,22)); d = ImageDraw.Draw(c)
    for i, (n, k, im, bl) in enumerate(builds):
        x = GAP + i*(w+GAP)
        crop = im.convert("RGB").crop((0, Y0, 160, Y1))
        c.paste(crop.resize((w, h), Image.NEAREST), (x, TOP))
        d.rectangle([x-2, TOP-2, x+w+1, TOP+h+1], outline=(70,70,80))
        d.text((x, TOP-90 if labels else 18), ("H%d  "%i if i else "")+n,
               fill=(255,215,120) if i else (150,150,160), font=F)
        if labels:
            for j, l in enumerate(textwrap.wrap(bl, 21)[:5]):
                d.text((x, TOP-70+j*13), l, fill=(140,140,150), font=FS)
    d.text((GAP, TOP+h+10), cap, fill=(120,120,130), font=FS)
    c.save(out); print(out, c.size)

sheet("hi_full.png",   1.0, 0,   480, "640x480 sidebar, 160x480, shown 1:1")
sheet("hi_top.png",    2.0, 0,   200, "header / radar bezel at 2x", labels=False)
sheet("hi_bottom.png", 2.0, 330, 480, "cells / meter / arrows at 2x", labels=False)
