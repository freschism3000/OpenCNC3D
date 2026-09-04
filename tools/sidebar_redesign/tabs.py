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

def three_frames(plate):
    """normal / hover / pressed, through states.py's own facet relight so a tab is lit
    the same way every other control on this panel is. chamfer 3 keeps the plate's end
    rivets intact; 0 squares the corners and 5 eats them."""
    import states
    mask = states.chamfer_mask(160, PLATE_H, 3)
    lab = states.facets(mask)
    f0 = plate
    f1 = states.relight(plate, mask, lab,
                        {states.UP: 1.22, states.LEFT: 1.55, states.FACE: 1.70,
                         states.DOWN: 1.45, states.RIGHT: 1.45}, floor=0.7)
    f2 = states.relight(plate, mask, lab,
                        {states.UP: 0.30, states.LEFT: 0.44, states.FACE: 0.66,
                         states.DOWN: 1.95, states.RIGHT: 1.45}, shift=(1, 1), floor=0.5)
    strip = Image.new("RGBA", (160 * 3, PLATE_H), (0, 0, 0, 0))
    for i, f in enumerate((f0, f1, f2)):
        strip.alpha_composite(f, (i * 160, 0))
    return strip


if __name__ == "__main__":
    three_frames(blank_plate()).save(f"{CH}/tab_plate.png")
    three_frames(plate_with("OPTIONS")).save(f"{CH}/tab_options.png")
    # SIDEBAR, the word that used to be BAKED INTO THE CHASSIS. Same font, same scale,
    # same pitch, same place -- it lands on plate rows 3..12 with letter runs 12 apart,
    # which is where the painted word was. It has to be its own plate because the bar
    # slides out from under it and a word baked into the bar would slide away with it.
    three_frames(plate_with("SIDEBAR")).save(f"{CH}/tab_sidebar.png")
    # DATABASE, the fourth plate. The 1995 pre-release tab strip carried four captions
    # (TACTICAL / OPTIONS / DATABASE / S.DIGEST) and this is the second of them coming
    # back. Same blank plate, same GRAD6FNT, same 1.75 scale and 12px pitch as OPTIONS,
    # so it is the same control wearing a different word rather than a new design: 99px
    # of lettering inside the 132px the plate has between its end rivets.
    three_frames(plate_with("DATABASE")).save(f"{CH}/tab_database.png")
    # And the chassis with that word erased. blank_plate() tiles a mirrored sample of the
    # plate across the middle, which covers the lettering and leaves the two end rivets.
    # chunks/chassis.png stays untouched: it is the source of truth.
    ch = Image.open(f"{CH}/chassis.png").convert("RGBA")
    ch.paste(blank_plate(), (0, 0))
    ch.save(f"{CH}/chassis_blank.png")
    # digit strip for the live credits readout, same font, same scale
    d = [text(str(n)) for n in range(10)]
    w = max(g.width for g in d); h = d[0].height
    strip = Image.new("RGBA", (w*10, h), (0,0,0,0))
    for i, g in enumerate(d): strip.alpha_composite(g, (i*w + (w-g.width)//2, 0))
    strip.save(f"{CH}/digits_strip.png")
    print("GRAD6FNT: SIDEBAR=%dpx  OPTIONS=%dpx  digit cell=%dx%d" %
          (text("SIDEBAR").width, text("OPTIONS").width, w, h))
