"""Assemble the delivered (spec-sized) art using the README layout."""
from PIL import Image
import os
SRC, W, H = "incoming", 160, 480
COL_X, ROW_Y = [18, 88], [210, 258, 306, 354, 402]
BTN = [(4, "repair"), (70, "sell"), (114, "map")]
ARR = [(20, "up"), (52, "down"), (90, "up"), (122, "down")]
def L(n): return Image.open(f"{SRC}/{n}.png").convert("RGBA")

def build(cameos=False):
    im = Image.new("RGBA", (W, H), (0,0,0,0))
    im.alpha_composite(L("panel_body"), (0, 0))
    im.alpha_composite(L("panel_header"), (0, 0))
    im.alpha_composite(L("panel_radar_bezel"), (0, 19))
    for x, n in BTN: im.alpha_composite(L(f"button_{n}"), (x, 184))
    im.alpha_composite(L("meter_assembled"), (0, 210))
    well, frame = L("cell_well"), L("cell_frame")
    names = ["FACT","E1","NUKE","E2","PROC","JEEP","WEAP","MTNK","HAND","APC"]
    i = 0
    for y in ROW_Y:
        for x in COL_X:
            im.alpha_composite(well, (x, y))
            if cameos and os.path.exists(f"art/{names[i]}.png"):
                c = Image.open(f"art/{names[i]}.png").convert("RGBA").resize((64,48), Image.NEAREST)
                im.alpha_composite(c, (x, y))
                im.alpha_composite(frame, (x, y))
            i += 1
    for x, d in ARR: im.alpha_composite(L(f"arrow_{d}"), (x, 454))
    return im

if __name__ == "__main__":
    a = build(); a.save("final_hud.png"); a.resize((W*3,H*3), Image.NEAREST).save("final_hud@3x.png")
    b = build(True); b.save("final_hud_cameos.png"); b.resize((W*3,H*3), Image.NEAREST).save("final_hud_cameos@3x.png")
    ref = Image.open("incoming/newhud_reference.png").convert("RGB").resize((W,H), Image.LANCZOS)
    from PIL import ImageDraw, ImageFont
    def fnt(s):
        try: return ImageFont.truetype("/System/Library/Fonts/Supplemental/Courier New Bold.ttf", s)
        except: return ImageFont.load_default()
    F = fnt(15); S = 3; GAP = 20; TOP = 34
    cols = [("YOUR REFERENCE", ref), ("ASSEMBLED FROM PIECES", a.convert("RGB"))]
    c = Image.new("RGB", (len(cols)*(W*S+GAP)+GAP, TOP+H*S+16), (18,18,22)); d = ImageDraw.Draw(c)
    for i,(n,im) in enumerate(cols):
        x = GAP + i*(W*S+GAP)
        c.paste(im.resize((W*S,H*S), Image.NEAREST), (x, TOP))
        d.rectangle([x-2,TOP-2,x+W*S+1,TOP+H*S+1], outline=(70,70,80))
        d.text((x, 10), n, fill=(255,215,120), font=F)
    c.save("final_compare.png"); print("compare ->", c.size)
