"""Credits readout, Options button, and synthesised pressed/hover states.

The pressed variants are PLACEHOLDERS derived from the delivered art (shifted down 1px
and darkened). They exist so interaction can be tested; real pressed art still needs
drawing at the sizes listed in the README."""
from PIL import Image, ImageEnhance
import numpy as np, os, json

HERE = os.path.dirname(os.path.abspath(__file__))
CH = os.path.join(HERE, "chunks")
cfg = json.load(open(f"{CH}/layout.json"))
chassis = Image.open(f"{CH}/chassis.png").convert("RGBA")

DIG = {
"0":["01110","10001","10011","10101","11001","10001","01110"],
"1":["00100","01100","00100","00100","00100","00100","01110"],
"2":["01110","10001","00001","00010","00100","01000","11111"],
"3":["11111","00010","00100","00010","00001","10001","01110"],
"4":["00010","00110","01010","10010","11111","00010","00010"],
"5":["11111","10000","11110","00001","00001","10001","01110"],
"6":["00110","01000","10000","11110","10001","10001","01110"],
"7":["11111","00001","00010","00100","01000","01000","01000"],
"8":["01110","10001","10001","01110","10001","10001","01110"],
"9":["01110","10001","10001","01111","00001","00010","01100"],
"$":["00100","01111","10100","01110","00101","11110","00100"],
",":["00000","00000","00000","00000","00110","00100","01000"],
" ":["00000"]*7,
}

def text(s, fg=(255,236,170), sh=(0,0,0)):
    w = sum(len(DIG[c][0])+1 for c in s) - 1
    im = Image.new("RGBA", (w+1, 9), (0,0,0,0)); px = im.load()
    x = 0
    for c in s:
        g = DIG[c]
        for j,row in enumerate(g):
            for i,ch in enumerate(row):
                if ch=="1":
                    px[x+i+1, j+2] = sh+(200,)
        for j,row in enumerate(g):
            for i,ch in enumerate(row):
                if ch=="1": px[x+i, j+1] = fg+(255,)
        x += len(g[0])+1
    return im

RAMP = [(0x10,0x10,0x10),(0x1c,0x1c,0x1a),(0x28,0x28,0x24),(0x34,0x34,0x2e),
        (0x40,0x40,0x38),(0x4e,0x4e,0x46),(0x5c,0x5c,0x52),(0x8a,0x8a,0x80),
        (0xc0,0xc0,0xb4),(0xe8,0xe0,0xd8)]

def plate(w, h, chamfer=4, face=3):
    """A clean bevelled plate in the HUD palette. Synthetic placeholder art."""
    im = Image.new("RGBA", (w, h), (0,0,0,0)); px = im.load()
    def inside(x, y):
        for (cx, cy) in ((0,0),(w-1,0),(0,h-1),(w-1,h-1)):
            if abs(x-cx)+abs(y-cy) < chamfer: return False
        return True
    for y in range(h):
        for x in range(w):
            if not inside(x, y): continue
            if y == 0 or x == 0:              c = RAMP[7]
            elif y == 1 or x == 1:            c = RAMP[6]
            elif y == h-1 or x == w-1:        c = RAMP[0]
            elif y == h-2 or x == w-2:        c = RAMP[1]
            else:                              c = RAMP[face]
            px[x, y] = c + (255,)
    return im

def credits_panel(value=12500, w=160, h=23):
    """PLACEHOLDER. Real art wanted at 160x23."""
    im = plate(w, h, 5, 2)
    px = im.load()
    for y in range(4, h-4):
        for x in range(6, w-6): px[x, y] = RAMP[0] + (255,)   # recessed readout window
    for x in range(6, w-6): px[x, 3] = RAMP[1] + (255,)
    s = "$" + f"{value:,}"
    t = text(s)
    im.alpha_composite(t, ((w - t.width)//2, (h - t.height)//2))
    return im

def options_button(w=64, h=23):
    """PLACEHOLDER. Real art wanted at 64x23, matching the REPAIR/SELL/MAP plates."""
    im = plate(w, h, 4, 4); px = im.load()
    LET = {"O":["0110","1001","1001","1001","0110"],"P":["1110","1001","1110","1000","1000"],
           "T":["11111","00100","00100","00100","00100"],"I":["111","010","010","010","111"],
           "N":["1001","1101","1011","1001","1001"],"S":["0111","1000","0110","0001","1110"]}
    word = "OPTIONS"; tw = sum(len(LET[c][0])+1 for c in word) - 1
    ox = (w-tw)//2; oy = (h-5)//2
    for c in word:
        g = LET[c]
        for j,row in enumerate(g):
            for i,ch2 in enumerate(row):
                if ch2 == "1" and 0 <= ox+i < w and 0 <= oy+j+1 < h:
                    px[ox+i, oy+j+1] = RAMP[0] + (255,)
        for j,row in enumerate(g):
            for i,ch2 in enumerate(row):
                if ch2 == "1" and 0 <= ox+i < w and 0 <= oy+j < h:
                    px[ox+i, oy+j] = RAMP[8] + (255,)
        ox += len(g[0]) + 1
    # amber lamp
    for x in range(w//2-4, w//2+5): px[x, h-5] = (0xf4,0xc0,0x18,255)
    return im

def chamfer_mask(w, h, r):
    """The plate silhouette. Buttons are rounded/chamfered rectangles, so lighting the
    whole crop put a bright rectangle round them instead of lighting the plate."""
    m = np.ones((h, w), bool)
    for (cx, cy) in ((0,0),(w-1,0),(0,h-1),(w-1,h-1)):
        for y in range(h):
            for x in range(w):
                if abs(x-cx) + abs(y-cy) < r: m[y, x] = False
    return m

def _shade(im, mask, mul, dy=0):
    """Scale brightness inside the mask only; everything outside becomes transparent so
    the chassis underneath shows through untouched."""
    src = np.array(im.convert("RGBA")).astype(float)
    h, w = mask.shape
    out = np.zeros((h, w, 4), np.uint8)
    body = np.clip(src[..., :3] * mul, 0, 255)
    if dy:
        body = np.roll(body, dy, axis=0)
        body[:dy] = np.clip(src[:dy, :, :3] * (mul*0.8), 0, 255)
    out[..., :3] = body
    out[..., 3] = np.where(mask, 255, 0)
    return Image.fromarray(out, "RGBA")

def pressed(im, amount=0.60, r=6):
    return _shade(im, chamfer_mask(im.width, im.height, r), amount, dy=1)

def hover(im, amount=1.30, r=6):
    return _shade(im, chamfer_mask(im.width, im.height, r), amount)

if __name__ == "__main__":
    out = os.path.join(HERE, "chunks")
    credits_panel().save(f"{out}/credits_panel.png")
    ob = options_button(); ob.save(f"{out}/button_options.png")
    pressed(ob).save(f"{out}/button_options_pressed.png")
    hover(ob).save(f"{out}/button_options_hover.png")
    BTN = {"repair": (8,185,63,211), "sell": (64,185,107,211), "map": (108,185,152,211)}
    for n,(x0,y0,x1,y1) in BTN.items():
        b = chassis.crop((x0,y0,x1,y1))
        b.save(f"{out}/btn_{n}.png"); pressed(b).save(f"{out}/btn_{n}_pressed.png")
        hover(b).save(f"{out}/btn_{n}_hover.png")
        print("button_%-7s %dx%d at (%d,%d)" % (n, x1-x0, y1-y0, x0, y0))
    ARR = [(18,452,50,478),(53,452,85,478),(88,452,120,478),(123,452,155,478)]
    for i,(x0,y0,x1,y1) in enumerate(ARR):
        a = chassis.crop((x0,y0,x1,y1))
        a.save(f"{out}/arr_{i}.png"); pressed(a).save(f"{out}/arr_{i}_pressed.png")
        hover(a).save(f"{out}/arr_{i}_hover.png")
    print("credits_panel 160x23, button_options %dx%d" % ob.size)
