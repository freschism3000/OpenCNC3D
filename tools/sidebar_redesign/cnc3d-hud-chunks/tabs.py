"""Tab-strip plates lettered with the game's OWN font.

GRAD6FNT is read straight out of dossidebar.pack (226 glyphs, 8x8 cells, 4bpp colour
classes). The nameplate in the delivered art is that font at 2x horizontal scale: 'SIDEBAR'
measures 84px there and GRAD6FNT gives 42px at an advance of width-2. So OPTIONS and
CREDITS are rendered the same way and sit at the same size as the nameplate.

Nothing here is hand-drawn. An earlier pass authored letterforms by eye; that was wrong,
the real font was in the pack all along."""
from PIL import Image
import numpy as np, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dosfont

HERE = os.path.dirname(os.path.abspath(__file__)); CH = os.path.join(HERE, "chunks")
chassis = Image.open(f"{CH}/chassis.png").convert("RGBA")
PLATE_H = 17
FONTS, PAL8 = dosfont.load()
XSPACING = -2                      # DB_FONT6_XSPACING, sidebar/dosbar.h:109
SCALE    = 1.75                    # fitted against the nameplate: 87% pixel agreement
PITCH    = 12                      # advance between glyph origins, also fitted
INK      = (0xd6, 0xd6, 0xcf)      # sampled off the nameplate lettering
SHADOW   = (0x10, 0x10, 0x0e)

def _glyph(f, c):
    """One glyph as a mask, scaled uniformly. Classes 0, 2 and 3 are background,
    shadow and outline; db_font_palette maps them to TBLACK and Buffer_Print skips
    any pixel whose looked-up colour is 0."""
    g = dosfont.classes(f, c)
    if g is None: return None
    m = np.array([[0 if cl in (0,2,3) else 255 for cl in row] for row in g], np.uint8)
    im = Image.fromarray(m, "L")
    return im.resize((int(round(im.width*SCALE)), int(round(im.height*SCALE))), Image.NEAREST)

def text(s, font="GRAD6FNT"):
    f = FONTS[font]
    gs = [(_glyph(f, c)) for c in s]
    gs = [g for g in gs if g is not None]
    if not gs: return Image.new("RGBA", (1,1), (0,0,0,0))
    w = PITCH*(len(gs)-1) + gs[-1].width
    h = gs[0].height
    im = Image.new("RGBA", (w, h), (0,0,0,0)); px = im.load()
    x = 0
    for g in gs:
        gp = g.load()
        for y in range(g.height):
            for i in range(g.width):
                if gp[i, y] and 0 <= x+i < w: px[x+i, y] = INK + (255,)
        x += PITCH
    a = np.array(im); m = a[..., 3] > 0
    sh = np.zeros((im.height, im.width, 4), np.uint8)
    sh[..., 0], sh[..., 1], sh[..., 2] = SHADOW
    sh[..., 3] = np.where(m, 205, 0)
    out = Image.new("RGBA", (im.width+1, im.height+1), (0,0,0,0))
    out.alpha_composite(Image.fromarray(sh, "RGBA"), (1, 1))
    out.alpha_composite(im, (0, 0))
    return out

def blank_plate():
    p = chassis.crop((0, 0, 160, PLATE_H)).copy()
    sample = p.crop((14, 0, 26, PLATE_H))
    flip = sample.transpose(Image.FLIP_LEFT_RIGHT)
    x, i = 14, 0
    while x < 146:
        t = sample if i % 2 == 0 else flip
        w = min(t.width, 146 - x)
        p.paste(t.crop((0, 0, w, PLATE_H)), (x, 0))
        x += t.width; i += 1
    return p

def plate_with(s):
    p = blank_plate(); t = text(s)
    p.alpha_composite(t, ((160 - t.width)//2, (PLATE_H - t.height)//2))
    return p

if __name__ == "__main__":
    blank_plate().save(f"{CH}/tab_plate.png")
    plate_with("OPTIONS").save(f"{CH}/tab_options.png")
    # digit strip for the live credits readout, same font, same scale
    d = [text(str(n)) for n in range(10)]
    w = max(g.width for g in d); h = d[0].height
    strip = Image.new("RGBA", (w*10, h), (0,0,0,0))
    for i, g in enumerate(d): strip.alpha_composite(g, (i*w + (w-g.width)//2, 0))
    strip.save(f"{CH}/digits_strip.png")
    print("GRAD6FNT: SIDEBAR=%dpx  OPTIONS=%dpx  digit cell=%dx%d" %
          (text("SIDEBAR").width, text("OPTIONS").width, w, h))
