from PIL import Image, ImageDraw, ImageFont
import sys, os, numpy as np; sys.path.insert(0, ".")
import kit as K, hires as X, machined_hi as MH

def font(b, s):
    p = "/System/Library/Fonts/Supplemental/Courier New%s.ttf" % (" Bold" if b else "")
    try: return ImageFont.truetype(p, s)
    except Exception: return ImageFont.load_default()
F, FS = font(True, 16), font(False, 12)

old_dos = K.upscale(K.assemble_original(), 2)
gen     = X.build(X.DESIGNS["H1-machined"])
new     = Image.open("machined_v2.png")
cols = [("DOS ORIGINAL @2x", old_dos, "80x200 doubled"),
        ("PREVIOUS PASS",    gen,     "generated bevels, smooth gradients"),
        ("PIXEL-ART PASS",   new,     "faceted ramps, hand detail, relief coin")]

def sheet(out, S, Y0, Y1, cap):
    GAP, TOP = 22, 74
    w, h = int(160*S), int((Y1-Y0)*S)
    c = Image.new("RGB", (len(cols)*(w+GAP)+GAP, TOP+h+30), (18,18,22)); d = ImageDraw.Draw(c)
    for i, (n, im, sub) in enumerate(cols):
        x = GAP + i*(w+GAP)
        crop = im.convert("RGB").crop((0, Y0, 160, Y1))
        c.paste(crop.resize((w, h), Image.NEAREST), (x, TOP))
        d.rectangle([x-2, TOP-2, x+w+1, TOP+h+1], outline=(70,70,80))
        d.text((x, 20), n, fill=(255,215,120) if i == 2 else (150,150,160), font=F)
        d.text((x, 42), sub, fill=(130,130,140), font=FS)
    d.text((GAP, TOP+h+10), cap, fill=(120,120,130), font=FS)
    c.save(out); print(out, c.size)

sheet("v2_full.png",   1.0, 0,   480, "full sidebar, 160x480, shown 1:1")
sheet("v2_detail.png", 3.0, 0,   150, "header, bezel and medallion at 3x")
sheet("v2_cells.png",  3.0, 300, 480, "cells, meter and arrows at 3x")
for n, im, _ in cols[1:]:
    a = np.array(im.convert("RGB")).reshape(-1,3)
    print("%-16s %6d distinct colours" % (n, len(np.unique(a, axis=0))))
