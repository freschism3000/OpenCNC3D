"""Place a native panel on screen exactly as menu/enhanced_menu.cpp's em_draw() does:
integer scale h/693, the title offset, the backdrop (integer fit on the main menu, cover
fill on title dialogs, the battlefield under 30% black in a mission), the wordmark, the
outer emblems, the legal footer and the pointer."""
import math, os, numpy as np
from PIL import Image
import paths
import chassis as C, fontrender as F, ecur

SRC = paths.WORK
LEGAL_W, LEGAL_H = 640, 28
BACKDROP = Image.open(os.path.join(SRC, 'ra_title_backdrop_clean.png')).convert('RGB')
BACKDROP_LO = Image.open(os.path.join(SRC, 'ra_title_backdrop_clean_lo.png')).convert('RGB')
WORD_HI = Image.open(os.path.join(SRC, 'ra_wordmark_hi.png')).convert('RGBA')
WORD_LO = Image.open(os.path.join(SRC, 'ra_wordmark_lo.png')).convert('RGBA')
# The battlefield behind in-mission screens: a saved Red Alert Enhanced game capture
# (RAMENU_BATTLE names another); without one, a plain dark field.
BATTLE = Image.open(paths.BATTLE).convert('RGB') if os.path.exists(paths.BATTLE) else Image.new('RGB', (1280, 720), (20, 26, 18))
EMB = {k: Image.open(os.path.join(SRC, 'emblem_%s.png' % k)).convert('RGBA') for k in ('Allied', 'Soviet', 'AlliedGlobe')}
_EC = None
LEGAL = ("OpenCNC3D is an unofficial fan project, not affiliated with or endorsed by Electronic Arts.\n"
         "Command & Conquer: Red Alert is a trademark of Electronic Arts Inc. Created by freschism")

def legal_scale(w, h): return max(1, min(w // LEGAL_W, h // 480))

def layout(pw, ph, fbw, fbh, title):
    s = max(1, fbh // 693)
    tw = max(pw, 1016) if title else pw
    while s > 1 and (tw * s > fbw - 40 or ph * s > fbh - 20): s -= 1
    x = (fbw - pw * s) // 2; y = (fbh - ph * s) // 2 + (52 * s if title else 0)
    if title: y = min(y, fbh - ph * s - LEGAL_H * legal_scale(fbw, fbh))
    return s, x, y

def emblem(kind, size):
    e = EMB[kind]; inner = int(size * 0.86)
    return e.resize((inner, inner), Image.LANCZOS), (size - inner) // 2

def plate(kind, w=136, h=120):
    """The details plate (TD's faction-GDI/Nod): the emblem on a steel tile cut from the
    dialog's own rail texture, with a dark inset."""
    T = C.THEMES['red']; t, _ = C.piece('DD-TOP', 0, T)
    steel = np.array(t.convert('RGB'))[0:4, :, :]
    tile = np.zeros((h, w, 3), np.uint8)
    for y in range(h): tile[y] = np.resize(np.roll(steel[y % 4], y * 29, axis=0), (w, 3))
    img = Image.fromarray((tile * 0.55).astype(np.uint8)).convert('RGBA')
    C.rect(img, 6, 6, w - 12, h - 12, (8, 6, 6), 200)
    C.rect(img, 0, 0, w, 1, (190, 190, 190)); C.rect(img, 0, 0, 1, h, (190, 190, 190)); C.rect(img, 0, h - 1, w, 1, (30, 30, 30)); C.rect(img, w - 1, 0, 1, h, (30, 30, 30))
    e, off = emblem(kind, h - 14); img.alpha_composite(e, ((w - e.width) // 2, 7 + off))
    return img

def legal_strip(text, pal14=(175, 183, 183)):
    strip = Image.new('RGBA', (LEGAL_W, LEGAL_H), (0, 0, 0, 0))
    lines = text.split('\n'); block = 8 + (len(lines) - 1) * 10; y = (LEGAL_H - block) // 2
    for ln in lines:
        x = (LEGAL_W - F.width(ln.upper(), scale=1)) // 2
        F.draw(strip, ln.upper(), x, y, pal14, scale=1); y += 10
    return strip

def cursor(canvas, x, y, scale):
    global _EC
    if _EC is None: _EC = ecur.load(os.path.join(SRC, 'ecur.bin'))
    a = np.array(canvas.convert('RGB')).astype(np.float32) / 255
    ecur.draw(a, x, y, scale, *_EC)
    return Image.fromarray((np.clip(a, 0, 1) * 255).astype(np.uint8)).convert('RGBA')

def compose(view, bg='title', title=False, main=False, fb=(2560, 1440), pointer=None, outer=True):
    """bg 'title': the title backdrop (integer fit when title, cover fill otherwise);
    bg 'mission': the battlefield under 30% black. title: the panel sits under the
    wordmark (the main menu and option pages opened from the title). main: the outer
    emblems and the legal footer (the main menu only)."""
    fbw, fbh = fb; pw, ph = view.img.size
    s, x, y = layout(pw, ph, fbw, fbh, title)
    can = Image.new('RGBA', (fbw, fbh), (0, 0, 0, 255))
    if bg == 'mission':
        b = BATTLE.resize((BATTLE.width * (fbw // BATTLE.width), BATTLE.height * (fbh // BATTLE.height)), Image.NEAREST)
        can.paste(b, ((fbw - b.width) // 2, (fbh - b.height) // 2)); C.rect(can, 0, 0, fbw, fbh, (0, 0, 0), 77)
    else:
        # the HIRES title (640x400) when it fits at least twice, else the LORES one (320x200)
        a = BACKDROP if min(fbw // 640, fbh // 400) >= 2 else BACKDROP_LO
        bs = max(1, min(fbw // a.width, fbh // a.height)) if title else max(1, max(math.ceil(fbw / a.width), math.ceil(fbh / a.height)))
        bd = a.resize((a.width * bs, a.height * bs), Image.NEAREST)
        can.paste(bd, ((fbw - bd.width) // 2, (fbh - bd.height) // 2))
    if title and bg == 'title':
        # the lettering, split from the backdrop: the HIRES cut at s/2 when s is even, the
        # LORES cut at s when it is odd, so it always lands on whole pixels
        wm, ws = (WORD_HI, s // 2) if s % 2 == 0 else (WORD_LO, s)
        wm = wm.resize((wm.width * ws, wm.height * ws), Image.NEAREST)
        can.alpha_composite(wm, ((fbw - wm.width) // 2, max(0, y - wm.height - 16 * s)))
    panel = view.img.resize((pw * s, ph * s), Image.NEAREST)
    can.alpha_composite(panel, (x, y))
    for b in view.badges:
        kind, bx, by, size = b[:4]
        if len(b) > 4 and b[4] == 'plate':
            p = plate(kind); p = p.resize((p.width * s, p.height * s), Image.NEAREST); can.alpha_composite(p, (x + bx * s, y + by * s))
        else:
            e, off = emblem(kind, size * s); can.alpha_composite(e, (x + bx * s + off, y + by * s + off))
    if main and outer:
        size = min(200, max(0, (fbw - pw * s) // 2 // s - 36))
        if size >= 80:
            for f, kind in enumerate(('Allied', 'Soviet')):
                qx = (pw + (fbw - x - pw * s) // s // 2 - size // 2) if f else (-x // s // 2 - size // 2)
                qy = (fbh - y - max(36 * s, LEGAL_H * legal_scale(fbw, fbh) + 8)) // s - size
                e, off = emblem(kind, size * s); can.alpha_composite(e, (x + qx * s + off, y + qy * s + off))
    if main:
        ls = legal_scale(fbw, fbh); st = legal_strip(LEGAL); st = st.resize((st.width * ls, st.height * ls), Image.NEAREST)
        can.alpha_composite(st, ((fbw - st.width) // 2, fbh - st.height))
    if pointer is not None:
        can = cursor(can, x + pointer[0] * s, y + pointer[1] * s, s)
    return can.convert('RGB'), (s, x, y)
