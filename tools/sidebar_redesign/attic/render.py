"""Render the drafts: contact sheets plus per-design PNGs."""
from PIL import Image, ImageDraw, ImageFont
import sys, os, textwrap; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import kit as K, designs as D

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "drafts")
os.makedirs(OUT, exist_ok=True)
def font(b, s):
    p = "/System/Library/Fonts/Supplemental/Courier New%s.ttf" % (" Bold" if b else "")
    try: return ImageFont.truetype(p, s)
    except Exception: return ImageFont.load_default()
F, FS = font(True, 15), font(False, 11)

builds = [("ORIGINAL", "", K.assemble_original(), "as it ships today")]
for k, t in D.DESIGNS.items():
    b = K.snap(D.build(t))
    b.save(f"{OUT}/{k}.png"); K.upscale(b, 4).save(f"{OUT}/{k}@4x.png")
    builds.append((t["name"].upper(), k, b, t["blurb"]))
K.assemble_original().save(f"{OUT}/ORIGINAL.png")

def sheet(out, S, Y0, Y1, cap, labels=True):
    GAP, TOP = 18, (74 if labels else 40)
    w, h = 80*S, (Y1-Y0)*S
    c = Image.new("RGB", (len(builds)*(w+GAP)+GAP, TOP+h+26), (18,18,22))
    d = ImageDraw.Draw(c)
    for i, (n, k, im, bl) in enumerate(builds):
        x = GAP + i*(w+GAP)
        d.rectangle([x-2, TOP-2, x+w+1, TOP+h+1], outline=(70,70,80))
        c.paste(K.upscale(im.convert("RGB").crop((0,Y0,80,Y1)), S), (x, TOP))
        d.text((x, TOP-56 if labels else 16), ("D%d  "%i if i else "")+n,
               fill=(255,215,120) if i else (150,150,160), font=F)
        if labels:
            for j, l in enumerate(textwrap.wrap(bl, 32)[:3]):
                d.text((x, TOP-36+j*13), l, fill=(140,140,150), font=FS)
    d.text((GAP, TOP+h+8), cap, fill=(120,120,130), font=FS)
    c.save(out); print(out, c.size)

sheet("sheet_full.png",   3, 0,   200, "full sidebar at 3x, snapped to the game's real 248-colour palette")
sheet("sheet_top.png",    5, 0,   92,  "header / radar bezel / buttons at 5x", labels=False)
sheet("sheet_bottom.png", 5, 88,  200, "power meter / cell frames / arrows at 5x", labels=False)
