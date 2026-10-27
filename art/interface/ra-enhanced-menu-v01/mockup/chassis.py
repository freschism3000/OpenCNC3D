"""Red Alert Enhanced menu chassis, built from Red Alert's own Win95 dialog pieces.

Native unit: one HIRES pixel (the RA Win95 dialog art is drawn 1:1), the same unit the
Tiberian Dawn Enhanced panels use, so every TD seat rectangle carries over unchanged.
"""
import os, numpy as np
from PIL import Image
import paths
import fontrender as F

SRC = paths.WORK

# ---- palettes -------------------------------------------------------------------------
# Index 171/174 are the dialog field and its grid lines; 192..199 the dialog colour ramp
# (PALETTE.CPS row 10, PCOLOR_DIALOG_BLUE). The title palette makes them red, the
# scenario palette blue: the 1996 game shows red dialogs at the title, blue in a mission.
def _pcx_pal():
    im = Image.open(os.path.join(SRC, 'TITLE.PCX')); p = im.getpalette()
    return [tuple(p[i*3:i*3+3]) for i in range(256)]
def _scen_pal():
    from ramix import RaMix
    t = RaMix(os.path.join(paths.RADATA, 'LOCAL.MIX')).read('TEMPERAT.PAL')
    return [tuple(min(255, c*4 + c//16) for c in t[i*3:i*3+3]) for i in range(256)]
PAL = {'red': _pcx_pal(), 'blue': _scen_pal()}

GOLD = (231, 167, 0)          # title palette 162: the C&C lettering gold
GOLD_HI = (255, 191, 35)      # 178
BRASS = (167, 123, 19)        # 123: the rivet brass
BRASS_HI = (207, 167, 67)     # 124
WHITE = (230, 222, 206)
BLACK = (0, 0, 0)

class Theme:
    def __init__(self, name):
        P = PAL[name]; self.name = name; self.P = P
        # The 1996 field, darkened (62% field, 50% grid) so plates and text separate.
        self.field = tuple(int(c * .62) for c in P[171]); self.line = tuple(int(c * .5) for c in P[174])
        self.ramp = [P[i] for i in range(192, 200)]
        self.text = WHITE
        self.muted = (196, 160, 150) if name == 'red' else (160, 178, 204)
        self.accent = GOLD
        self.well_lit = self.ramp[5] if name == 'red' else self.ramp[4]
        self.row_on = (110, 14, 6) if name == 'red' else (26, 56, 120)
        self.row_hover = (70, 16, 10) if name == 'red' else (22, 40, 84)
THEMES = {n: Theme(n) for n in ('red', 'blue')}

# ---- original pieces ------------------------------------------------------------------
def _idx(name):
    return open(os.path.join(SRC, 'HIRES', name + '.idx'), 'rb').read()
_SHAPES = {'DD-BKGND': (4, 312, 192), 'DD-EDGE': (2, 16, 6), 'DD-LEFT': (1, 14, 200),
           'DD-RIGHT': (1, 14, 200), 'DD-TOP': (1, 320, 16), 'DD-BOTM': (1, 320, 16),
           'DD-CRNR': (4, 23, 24), 'BTN-UP': (3, 16, 16), 'BTN-DN': (3, 16, 16),
           'BTN-PL': (3, 24, 18), 'BTN-ST': (3, 24, 18)}
def piece(name, frame, theme):
    n, w, h = _SHAPES[name]; d = _idx(name)[frame*w*h:(frame+1)*w*h]
    a = np.frombuffer(d, np.uint8).reshape(h, w); P = np.array(theme.P, np.uint8)
    rgba = np.zeros((h, w, 4), np.uint8); rgba[..., :3] = P[a]; rgba[..., 3] = np.where(a == 0, 0, 255)
    return Image.fromarray(rgba, 'RGBA'), a

_BK = np.load(os.path.join(SRC, 'bkgnd_idx.npy'))       # 384 x 624, values 171 / 174
_GRID = np.zeros_like(_BK, bool)
_GRID[:, 38::48] = True; _GRID[28::48, :] = True
_MARKS = (_BK == 174) & ~_GRID                          # the diagonals and crosshair marks

def field(w, h, theme, ox, oy):
    """The dialog field at any size: the grid continued at its own 48 pixel pitch and
    phase, and the original diagonals and marks centred as Dialog_Box centres them,
    mirrored outward so every line runs on without a cut."""
    out = np.zeros((h, w, 3), np.uint8); out[:] = theme.field
    yy, xx = np.mgrid[0:h, 0:w]
    grid = ((xx - ox - 38) % 48 == 0) | ((yy - oy - 28) % 48 == 0)
    bh, bw = _MARKS.shape
    tx = (xx - ox) // bw; ty = (yy - oy) // bh
    lx = (xx - ox) % bw; ly = (yy - oy) % bh
    lx = np.where(tx % 2 == 0, lx, bw - 1 - lx); ly = np.where(ty % 2 == 0, ly, bh - 1 - ly)
    marks = _MARKS[ly, lx]
    out[grid | marks] = theme.line
    return out

def frame(w, h, theme):
    img = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    cx, cy = w // 2, h // 2
    f = field(w, h, theme, cx - 312, cy - 192)
    # Enhanced: the field sits a step below the rails (a two pixel fall-off of light).
    a = f.astype(np.int16)
    for i, k in enumerate((0.55, 0.75)):
        a[16+i, :] = a[16+i, :] * k; a[h-17-i, :] = a[h-17-i, :] * (k + .15)
        a[:, 14+i] = a[:, 14+i] * k; a[:, w-15-i] = a[:, w-15-i] * (k + .15)
    img.paste(Image.fromarray(np.clip(a, 0, 255).astype(np.uint8), 'RGB'), (0, 0))
    e0, _ = piece('DD-EDGE', 0, theme); e1, _ = piece('DD-EDGE', 1, theme)
    for yy in range(0, h, 6):
        img.alpha_composite(e0, (14, yy)) if yy + 6 <= h else img.paste(e0.crop((0, 0, 16, h - yy)), (14, yy), e0.crop((0, 0, 16, h - yy)))
        img.alpha_composite(e1, (w - 30, yy)) if yy + 6 <= h else img.paste(e1.crop((0, 0, 16, h - yy)), (w - 30, yy), e1.crop((0, 0, 16, h - yy)))
    l, _ = piece('DD-LEFT', 0, theme); r, _ = piece('DD-RIGHT', 0, theme)
    for k in range(-4, 5):
        y = cy + k * 200
        if y + 200 > 0 and y < h:
            img.paste(l, (0, y), l); img.paste(r, (w - 14, y), r)
    t, _ = piece('DD-TOP', 0, theme); b, _ = piece('DD-BOTM', 0, theme)
    for k in range(-4, 4):
        x = cx + k * 320
        if x + 320 > 0 and x < w:
            img.paste(t, (x, 0), t); img.paste(b, (x, h - 16), b)
    for i, (x, y) in enumerate([(0, 0), (w - 23, 0), (0, h - 24), (w - 23, h - 24)]):
        c, _ = piece('DD-CRNR', i, theme); img.paste(c, (x, y), c)
    return img

# ---- drawing helpers on an RGBA canvas ------------------------------------------------
def rect(img, x, y, w, h, col, alpha=255):
    if w <= 0 or h <= 0: return
    ov = Image.new('RGBA', (w, h), col + (alpha,)); img.alpha_composite(ov, (x, y))

def text(img, s, x, y, col=WHITE, center=False):
    return F.draw(img, s, x, y, col, center=center)
def textwidth(s):
    return F.width(s)
def fitted(s, w):
    if textwidth(s) <= w: return s
    while s and textwidth(s + '..') > w: s = s[:-1]
    return s + '..'
def wrapped(s, width):
    out = []
    for para in s.split('\n'):
        line = ''
        for word in para.split():
            if line and textwidth(line + ' ' + word) > width: out.append(line); line = word
            else: line = (line + ' ' + word).strip()
        out.append(line)
    return out
def note(img, s, x, y, width, maxlines=99, col=None, pitch=26, theme=None, newest=False):
    col = col or (theme.muted if theme else (190, 170, 160))
    lines = wrapped(s, width)
    start = max(0, len(lines) - maxlines) if newest else 0
    for i, ln in enumerate(lines[start:start + maxlines]): text(img, ln, x, y + i * pitch, col)

def well(img, title, x, y, w, h, theme, overlay=False):
    """A recessed liner with cut corners; the field shows through, darkened."""
    a = np.array(img); reg = a[y:y+h, x:x+w].astype(np.float32)
    if overlay:
        reg[..., :3] = np.array((14, 4, 2) if theme.name == 'red' else (6, 10, 24), np.float32)
    else:
        reg[..., :3] *= 0.36
    a[y:y+h, x:x+w] = reg.astype(np.uint8); img.paste(Image.fromarray(a), (0, 0))
    dark = (6, 2, 2)
    rect(img, x+4, y, w-8, 2, dark); rect(img, x, y+4, 2, h-8, dark)
    rect(img, x+w-2, y+4, 2, h-8, dark); rect(img, x+4, y+h-2, w-8, 2, dark)
    px = img.load()
    for i in range(4):
        for (qx, qy) in ((x+3-i, y+i), (x+w-4+i, y+i), (x+3-i, y+h-1-i), (x+w-4+i, y+h-1-i)):
            if 0 <= qx < img.width and 0 <= qy < img.height: px[qx, qy] = dark + (255,)
    rect(img, x+4, y+h-2, w-8, 1, theme.well_lit, 200); rect(img, x+w-2, y+5, 1, h-10, theme.well_lit, 200)
    if title:
        text(img, title, x+18, y+14, theme.accent)
        rect(img, x+16, y+40, w-32, 1, (4, 2, 2)); rect(img, x+16, y+41, w-32, 1, theme.well_lit, 160)

# ---- buttons --------------------------------------------------------------------------
_RAIL = None
def _grain(w, h):
    """Brushed grain borrowed from the dialog's own steel rail (DD-TOP), as luminance."""
    global _RAIL
    if _RAIL is None:
        t, idx = piece('DD-TOP', 0, THEMES['red'])
        g = np.array(t.convert('L'), np.float32)[1:4, :]       # the upper steel band
        _RAIL = np.tile(g, (1, 1))
    g = _RAIL
    rows = []
    for yy in range(h):
        row = np.roll(g[yy % g.shape[0]], (yy * 37) % g.shape[1])
        rows.append(np.resize(row, w))
    a = np.array(rows); return (a - a.mean()) / (a.std() + 1e-6)

def _mix(a, b, t):
    return tuple(int(a[i] * (1 - t) + b[i] * t) for i in range(3))

def button(w, h, state, theme):
    """normal, hover, active, activehover, pressed, disabled."""
    R = theme.ramp
    if state == 'disabled':
        base = _mix(R[6], (60, 56, 54), .7); hi = _mix(R[4], (110, 104, 100), .7); lo = (24, 20, 20)
    elif state == 'pressed':
        base = R[7]; hi = (10, 2, 2); lo = R[3]
    elif state == 'hover':
        base = _mix(R[6], R[5], .55); hi = R[1]; lo = R[7]
    elif state == 'active':
        base = R[4]; hi = R[0]; lo = R[7]
    elif state == 'activehover':
        base = R[3]; hi = R[0]; lo = R[6]
    else:
        base = R[6]; hi = R[2]; lo = R[7]
    img = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    g = _grain(w, h)
    a = np.zeros((h, w, 4), np.float32)
    shade = np.linspace(1.10, 0.86, h)[:, None]
    for c in range(3): a[..., c] = base[c] * shade * (1 + 0.06 * g)
    a[..., 3] = 255
    # bevel: light from the upper left, two pixels, then a black outline with cut corners
    a[1, 1:w-1, :3] = hi; a[2, 2:w-2, :3] = _mix(hi, base, .5)
    a[1:h-1, 1, :3] = hi; a[2:h-2, 2, :3] = _mix(hi, base, .5)
    a[h-2, 1:w-1, :3] = lo; a[h-3, 2:w-2, :3] = _mix(lo, base, .5)
    a[1:h-1, w-2, :3] = lo; a[2:h-2, w-3, :3] = _mix(lo, base, .5)
    a[0, :, :3] = 0; a[h-1, :, :3] = 0; a[:, 0, :3] = 0; a[:, w-1, :3] = 0
    for (x, y) in ((0, 0), (1, 0), (0, 1), (w-1, 0), (w-2, 0), (w-1, 1), (0, h-1), (1, h-1), (0, h-2), (w-1, h-1), (w-2, h-1), (w-1, h-2)):
        a[y, x, 3] = 0
    for (x, y) in ((1, 1), (w-2, 1), (1, h-2), (w-2, h-2)): a[y, x, :3] = 0
    # brass fasteners, the dialog corners' rivets at button size
    if w >= 60 and h >= 28 and state != 'disabled':
        for (x, y) in ((5, 5), (w-7, 5), (5, h-7), (w-7, h-7)):
            a[y:y+2, x:x+2, :3] = BRASS; a[y, x, :3] = BRASS_HI; a[y+1, x+1, :3] = (70, 46, 6)
    img = Image.fromarray(np.clip(a, 0, 255).astype(np.uint8), 'RGBA')
    return img

def arrow_glyph(theme):
    """The down arrow out of BTN-DN, as a mask (its bright pixels)."""
    _, idx = piece('BTN-DN', 0, theme)
    m = np.isin(idx, (194, 193)) ; return m

def checkbox(img, x, y, on, disabled, theme):
    rect(img, x, y, 22, 22, (150, 150, 150)); rect(img, x+1, y+1, 20, 20, (8, 4, 4))
    rect(img, x+1, y+1, 20, 1, (60, 60, 60)); rect(img, x+2, y+20, 18, 1, (40, 30, 30)); rect(img, x+20, y+2, 1, 18, (40, 30, 30))
    if on:
        col = (150, 130, 100) if disabled else theme.accent
        for (px_, py_) in ((5, 10), (7, 12), (9, 14), (11, 12), (13, 10), (15, 8), (17, 6)):
            rect(img, x+px_-1, y+py_-1, 3, 3, col)
