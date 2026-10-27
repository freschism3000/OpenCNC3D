#!/usr/bin/env python3
"""Bake Red Alert's Enhanced menu art kit into ra3d/enhanced-menu.pack.

The Tiberian Dawn Enhanced menu (menu/enhanced_menu.cpp) draws its screens from a pack of
native RGBA art: frames, button states, the font, the backdrop and the wordmark. This is the
same pack for Red Alert, made from Red Alert's own 1996 data (data/radata) as the approved
design (art/interface/ra-enhanced-menu-v01) lays it out:

- the frames are Red Alert's Win95 Dialog_Box (redalert/dialog.cpp): the silver rails with
  their black channel (DD-EDGE, DD-LEFT, DD-RIGHT, DD-TOP, DD-BOTM), the brass corner plates
  (DD-CRNR) and the grid field (DD-BKGND), at their native HIRES pixels and at any size. The
  field runs on at its own 48 pixel pitch and phase, the diagonals and marks block centred as
  Dialog_Box centres it and mirrored outward so no line is cut; it is darkened to 62% (the
  grid to 50%) and falls away from the rails by two pixels;
- each frame and button comes in the two palettes the 1996 dialogs are drawn in: the title
  palette (TITLE.PCX), which makes them red, and the scenario palette (TEMPERAT.PAL), which
  makes them blue. The menu draws red at the title and blue in a mission;
- buttons follow the Tiberian Dawn Enhanced geometry and six states in the dialog's own
  colour ramp (PALETTE.CPS row 10, palette indices 192 to 199), with the rail's grain and the
  corner plates' brass as fasteners;
- the font is GRAD6FNT from LORES.MIX, drawn 2x by the menu, its advance table in the pack;
- the backdrop is the title without its lettering at both densities and the lettering is
  the wordmark, cut by the same outline (art/interface/ra-enhanced-menu-v01/kit);
- the intro still is a frame of PROLOG.VQA at the movie's own 320 x 156.

Usage: bake_ra_enhanced_menu.py OUT.pack   (writes OUT.json beside it, the proof list)
"""
import argparse, hashlib, io, json, os, struct, sys
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
for p in (ROOT / 'tools' / 'ra3d', ROOT / 'menu' / 'tools', ROOT / 'menu', ROOT / 'game'):
    sys.path.insert(0, str(p))
from ramix import RaMix                    # noqa: E402
from mixshp import Shape                   # noqa: E402
from bake_dossidebar import bake_font      # noqa: E402
from bake_dosmenu import load_cps          # noqa: E402

RADATA = ROOT / 'data' / 'radata'
KIT = ROOT / 'art' / 'interface' / 'ra-enhanced-menu-v01' / 'kit'
TITLE_ART = ROOT / 'menu' / 'art'
TITLE_CLEANUP = ROOT / 'art' / 'interface' / 'ra3d-title-cleanup-v01'
MAGIC = b'ENMENURA'

# ---- the 1996 sources -----------------------------------------------------------------
def mix(name): return RaMix(str(RADATA / name))
HIRES, LORES, LOCAL = mix('HIRES.MIX'), mix('LORES.MIX'), mix('LOCAL.MIX')

def title_palette():
    p = Image.open(io.BytesIO(HIRES.read('TITLE.PCX'))).getpalette()
    return [tuple(p[i * 3:i * 3 + 3]) for i in range(256)]
def scenario_palette():
    t = LOCAL.read('TEMPERAT.PAL')
    return [tuple(min(255, c * 4 + c // 16) for c in t[i * 3:i * 3 + 3]) for i in range(256)]
PAL = {'red': title_palette(), 'blue': scenario_palette()}

SHAPES = {'DD-BKGND': (4, 312, 192), 'DD-EDGE': (2, 16, 6), 'DD-LEFT': (1, 14, 200),
          'DD-RIGHT': (1, 14, 200), 'DD-TOP': (1, 320, 16), 'DD-BOTM': (1, 320, 16),
          'DD-CRNR': (4, 23, 24), 'BTN-UP': (3, 16, 16), 'BTN-DN': (3, 16, 16),
          'BTN-PL': (3, 24, 18), 'BTN-ST': (3, 24, 18)}
_IDX = {}
def indices(name, frame):
    if name not in _IDX:
        s = Shape(HIRES.read(name + '.SHP')); n, w, h = SHAPES[name]
        assert (s.frames, s.width, s.height) == (n, w, h), name
        _IDX[name] = [np.frombuffer(bytes(s.frame(i)), np.uint8).reshape(h, w) for i in range(n)]
    return _IDX[name][frame]

class Theme:
    WHITE = (230, 222, 206)
    GOLD = (231, 167, 0)          # title palette 162: the C&C lettering gold
    BRASS, BRASS_HI = (167, 123, 19), (207, 167, 67)
    def __init__(self, name):
        P = PAL[name]; self.name = name; self.P = P
        self.field = tuple(int(c * .62) for c in P[171]); self.line = tuple(int(c * .5) for c in P[174])
        self.ramp = [P[i] for i in range(192, 200)]
        self.text = self.WHITE
        self.muted = (196, 160, 150) if name == 'red' else (160, 178, 204)
        self.accent = self.GOLD
        self.well_lit = self.ramp[5] if name == 'red' else self.ramp[4]
        self.row_on = (110, 14, 6) if name == 'red' else (26, 56, 120)
        self.row_hover = (70, 16, 10) if name == 'red' else (22, 40, 84)
        self.overlay = (14, 4, 2) if name == 'red' else (6, 10, 24)
        self.track = (30, 6, 4) if name == 'red' else (8, 16, 40)
    def slots(self):
        """The colours the runtime reads, in the order enhanced_menu.cpp names them."""
        return [self.text, self.muted, self.accent, self.well_lit, self.row_on, self.row_hover] + \
               self.ramp + [self.overlay, self.track]
THEMES = {n: Theme(n) for n in ('red', 'blue')}

def piece(name, frame, theme):
    a = indices(name, frame); P = np.array(theme.P, np.uint8)
    rgba = np.zeros(a.shape + (4,), np.uint8); rgba[..., :3] = P[a]; rgba[..., 3] = np.where(a == 0, 0, 255)
    return Image.fromarray(rgba, 'RGBA')

# DD-BKGND's four quadrants as one 624 x 384 field of indices 171 (field) and 174 (lines)
_BK = np.vstack([np.hstack([indices('DD-BKGND', 0), indices('DD-BKGND', 1)]),
                 np.hstack([indices('DD-BKGND', 2), indices('DD-BKGND', 3)])])
_GRID = np.zeros_like(_BK, bool); _GRID[:, 38::48] = True; _GRID[28::48, :] = True
_MARKS = (_BK == 174) & ~_GRID                            # the diagonals and crosshair marks

def field(w, h, theme, ox, oy):
    out = np.zeros((h, w, 3), np.uint8); out[:] = theme.field
    yy, xx = np.mgrid[0:h, 0:w]
    grid = ((xx - ox - 38) % 48 == 0) | ((yy - oy - 28) % 48 == 0)
    bh, bw = _MARKS.shape
    tx = (xx - ox) // bw; ty = (yy - oy) // bh; lx = (xx - ox) % bw; ly = (yy - oy) % bh
    lx = np.where(tx % 2 == 0, lx, bw - 1 - lx); ly = np.where(ty % 2 == 0, ly, bh - 1 - ly)
    out[grid | _MARKS[ly, lx]] = theme.line
    return out

def frame(w, h, theme):
    img = Image.new('RGBA', (w, h), (0, 0, 0, 0)); cx, cy = w // 2, h // 2
    a = field(w, h, theme, cx - 312, cy - 192).astype(np.int16)
    for i, k in enumerate((0.55, 0.75)):            # the field a step below the rails
        a[16 + i, :] = a[16 + i, :] * k; a[h - 17 - i, :] = a[h - 17 - i, :] * (k + .15)
        a[:, 14 + i] = a[:, 14 + i] * k; a[:, w - 15 - i] = a[:, w - 15 - i] * (k + .15)
    img.paste(Image.fromarray(np.clip(a, 0, 255).astype(np.uint8), 'RGB'), (0, 0))
    e0, e1 = piece('DD-EDGE', 0, theme), piece('DD-EDGE', 1, theme)
    for yy in range(0, h, 6):
        if yy + 6 <= h:
            img.alpha_composite(e0, (14, yy)); img.alpha_composite(e1, (w - 30, yy))
        else:
            c0, c1 = e0.crop((0, 0, 16, h - yy)), e1.crop((0, 0, 16, h - yy))
            img.paste(c0, (14, yy), c0); img.paste(c1, (w - 30, yy), c1)
    l, r = piece('DD-LEFT', 0, theme), piece('DD-RIGHT', 0, theme)
    for k in range(-4, 5):
        y = cy + k * 200
        if y + 200 > 0 and y < h: img.paste(l, (0, y), l); img.paste(r, (w - 14, y), r)
    t, b = piece('DD-TOP', 0, theme), piece('DD-BOTM', 0, theme)
    for k in range(-4, 4):
        x = cx + k * 320
        if x + 320 > 0 and x < w: img.paste(t, (x, 0), t); img.paste(b, (x, h - 16), b)
    for i, (x, y) in enumerate([(0, 0), (w - 23, 0), (0, h - 24), (w - 23, h - 24)]):
        c = piece('DD-CRNR', i, theme); img.paste(c, (x, y), c)
    return img

_RAIL = None
def grain(w, h):
    """Brushed grain borrowed from the dialog's own steel rail (DD-TOP), as luminance."""
    global _RAIL
    if _RAIL is None:
        _RAIL = np.array(piece('DD-TOP', 0, THEMES['red']).convert('L'), np.float32)[1:4, :]
    g = _RAIL
    a = np.array([np.resize(np.roll(g[yy % g.shape[0]], (yy * 37) % g.shape[1]), w) for yy in range(h)])
    return (a - a.mean()) / (a.std() + 1e-6)

def mixc(a, b, t): return tuple(int(a[i] * (1 - t) + b[i] * t) for i in range(3))

def button(w, h, state, theme):
    R = theme.ramp
    if state == 'disabled': base = mixc(R[6], (60, 56, 54), .7); hi = mixc(R[4], (110, 104, 100), .7); lo = (24, 20, 20)
    elif state == 'pressed': base = R[7]; hi = (10, 2, 2); lo = R[3]
    elif state == 'hover': base = mixc(R[6], R[5], .55); hi = R[1]; lo = R[7]
    elif state == 'active': base = R[4]; hi = R[0]; lo = R[7]
    elif state == 'activehover': base = R[3]; hi = R[0]; lo = R[6]
    else: base = R[6]; hi = R[2]; lo = R[7]
    g = grain(w, h); a = np.zeros((h, w, 4), np.float32)
    shade = np.linspace(1.10, 0.86, h)[:, None]
    for c in range(3): a[..., c] = base[c] * shade * (1 + 0.06 * g)
    a[..., 3] = 255
    a[1, 1:w - 1, :3] = hi; a[2, 2:w - 2, :3] = mixc(hi, base, .5)
    a[1:h - 1, 1, :3] = hi; a[2:h - 2, 2, :3] = mixc(hi, base, .5)
    a[h - 2, 1:w - 1, :3] = lo; a[h - 3, 2:w - 2, :3] = mixc(lo, base, .5)
    a[1:h - 1, w - 2, :3] = lo; a[2:h - 2, w - 3, :3] = mixc(lo, base, .5)
    a[0, :, :3] = 0; a[h - 1, :, :3] = 0; a[:, 0, :3] = 0; a[:, w - 1, :3] = 0
    for (x, y) in ((0, 0), (1, 0), (0, 1), (w - 1, 0), (w - 2, 0), (w - 1, 1), (0, h - 1), (1, h - 1), (0, h - 2),
                   (w - 1, h - 1), (w - 2, h - 1), (w - 1, h - 2)):
        a[y, x, 3] = 0
    for (x, y) in ((1, 1), (w - 2, 1), (1, h - 2), (w - 2, h - 2)): a[y, x, :3] = 0
    if w >= 60 and h >= 28 and state != 'disabled':     # brass fasteners
        for (x, y) in ((5, 5), (w - 7, 5), (5, h - 7), (w - 7, h - 7)):
            a[y:y + 2, x:x + 2, :3] = Theme.BRASS; a[y, x, :3] = Theme.BRASS_HI; a[y + 1, x + 1, :3] = (70, 46, 6)
    return Image.fromarray(np.clip(a, 0, 255).astype(np.uint8), 'RGBA')

def plate(w=136, h=120):
    """The details plate behind a faction emblem: a steel tile cut from the dialog's own
    rail, darkened, with a dark inset and a one pixel bevel. The emblem is drawn on it."""
    steel = np.array(piece('DD-TOP', 0, THEMES['red']).convert('RGB'))[0:4, :, :]
    tile = np.zeros((h, w, 3), np.uint8)
    for y in range(h): tile[y] = np.resize(np.roll(steel[y % 4], y * 29, axis=0), (w, 3))
    img = Image.fromarray((tile * 0.55).astype(np.uint8)).convert('RGBA')
    def rect(x, y, rw, rh, col, alpha=255):
        img.alpha_composite(Image.new('RGBA', (rw, rh), col + (alpha,)), (x, y))
    rect(6, 6, w - 12, h - 12, (8, 6, 6), 200)
    rect(0, 0, w, 1, (190, 190, 190)); rect(0, 0, 1, h, (190, 190, 190))
    rect(0, h - 1, w, 1, (30, 30, 30)); rect(w - 1, 0, 1, h, (30, 30, 30))
    return img

class FontSrc:
    def __init__(self, res): self.a = mix(res + '.MIX')
    def read(self, n):
        v = self.a.read(n)
        return v if v is not None else LOCAL.read(n)

def font_assets():
    """GRAD6FNT from LORES.MIX as an atlas (shadow classes 1-3 black, the face white) and
    its advance table (glyph width less the font's one pixel overlap, as the 1996 print
    routine steps it)."""
    n, mw, mh, widths, _ytop, _rows, cells = bake_font(FontSrc('LORES'), 'GRAD6FNT')
    assert (mw, mh) == (8, 8), (mw, mh)
    atlas = np.zeros((mh, n * mw, 4), np.uint8)
    for c in range(n):
        for y in range(mh):
            for x in range(mw):
                v = cells[c * mw * mh + y * mw + x]
                if v: atlas[y, c * mw + x] = (0, 0, 0, 255) if v < 4 else (255, 255, 255, 255)
    adv = np.zeros((1, n, 4), np.uint8)
    for c in range(n): adv[0, c] = (max(0, widths[c] - 1), 0, 0, 255)
    return Image.fromarray(atlas, 'RGBA'), Image.fromarray(adv, 'RGBA')

# The lettering plate's outline at HIRES pixels, the one the clean plates were made with
from importlib import util as _u
_spec = _u.spec_from_file_location('cleanplate_hand', str(KIT / 'cleanplate_hand.py'))
_cp = _u.module_from_spec(_spec); _spec.loader.exec_module(_cp)
def wordmark(src, k):
    im = np.array(Image.open(src).convert('RGB')); H, W, _ = im.shape
    m = Image.new('L', (W, H), 0); ImageDraw.Draw(m).polygon([(x * k, y * k) for x, y in _cp.POLY], fill=255)
    mk = np.array(m) > 0; ys, xs = np.where(mk)
    rgba = np.dstack([im, np.where(mk, 255, 0).astype(np.uint8)])
    return Image.fromarray(rgba, 'RGBA').crop((xs.min(), ys.min(), xs.max() + 1, ys.max() + 1))

# Every frame the screens open, at the native sizes the Tiberian Dawn screens use, and the
# heights the title dialogs share with the main menu (compact).
FRAMES = {'main-frame': (572, 564), 'pause-frame': (708, 528), 'settings-frame': (676, 584),
          'visuals-frame': (1016, 636), 'confirm-frame': (676, 340), 'lobby-frame': (1216, 636)}
COMPACT = ('pause-frame', 'settings-frame', 'visuals-frame', 'confirm-frame')
BUTTONS = [(28, 28), (80, 40), (132, 32), (132, 40), (140, 40), (148, 40), (154, 40), (160, 40), (180, 40),
           (192, 40), (196, 40), (204, 40), (220, 40), (230, 40), (268, 40), (284, 40), (292, 40), (294, 40),
           (310, 40), (332, 40), (424, 40), (464, 40), (500, 40), (564, 40), (604, 40), (604, 48), (636, 48)]
STATES = ('normal', 'hover', 'active', 'activehover', 'pressed', 'disabled')

def assets():
    out = {}
    font, adv = font_assets(); out['font_menu'] = font; out['font_advance'] = adv
    for name, T in THEMES.items():
        th = np.zeros((1, 16, 4), np.uint8)
        for i, c in enumerate(T.slots()): th[0, i] = tuple(c) + (255,)
        out['theme-' + name] = Image.fromarray(th, 'RGBA')
        for f, (w, h) in FRAMES.items():
            if f == 'main-frame' and name == 'blue': continue      # the main menu is only ever at the title
            out['%s-%s' % (f, name)] = frame(w, h, T)
        for (w, h) in BUTTONS:
            for s in STATES: out['button-%d-%d-%s-%s' % (w, h, s, name)] = button(w, h, s, T)
        for g in ('BTN-PL', 'BTN-ST'):
            out['%s-%s' % (g.lower(), name)] = piece(g, 0, T)
    for f in COMPACT:
        out['%s-compact-red' % f] = frame(FRAMES[f][0], FRAMES['main-frame'][1], THEMES['red'])
    # the eight multiplayer colours: each 1996 remap row of PALETTE.CPS (GOLD LTBLUE RED GREEN
    # ORANGE GREY BLUE BROWN), its column 3 (the unit paint's lit tone), in the scenario palette
    px, _ = load_cps(LOCAL.read('PALETTE.CPS'))
    rc = np.zeros((1, 8, 4), np.uint8)
    for r in range(8): rc[0, r] = tuple(PAL['blue'][px[r * 320 + 3]]) + (255,)
    out['remap-colours'] = Image.fromarray(rc, 'RGBA')
    out['plate'] = plate()
    out['title-backdrop'] = Image.open(TITLE_CLEANUP / 'backdrop-hi.png').convert('RGBA')
    out['title-backdrop-lo'] = Image.open(TITLE_CLEANUP / 'backdrop-lo.png').convert('RGBA')
    out['wordmark'] = Image.open(TITLE_CLEANUP / 'wordmark-hi.png').convert('RGBA')
    out['wordmark-lo'] = Image.open(TITLE_CLEANUP / 'wordmark-lo.png').convert('RGBA')
    out['intro-frame'] = Image.open(KIT / 'intro-still.png').convert('RGBA')
    # the mission briefing papers Restate_Mission puts behind the box (redalert/scenario.cpp), cut to
    # the mission briefing well (600 x 350, centred) and darkened to 62% under the text
    for side, pcx in (('allied', 'ALIPAPER'), ('soviet', 'SOVPAPER')):
        im = Image.open(io.BytesIO(HIRES.read(pcx + '.PCX'))).convert('RGB')
        w, h = 600, FRAMES['settings-frame'][1] - 234
        a = np.array(im.crop(((640 - w) // 2, (400 - h) // 2, (640 - w) // 2 + w, (400 - h) // 2 + h))).astype(np.float32) * 0.62
        out['paper-' + side] = Image.fromarray(a.astype(np.uint8)).convert('RGBA')
    return out

def bake(output):
    records = []; proofs = {}
    arts = assets()
    # the skirmish maps' pictures, when the module beside the pack has them
    # (tools/ra3d/bake-ra-map-previews.py writes ra3d/previews/SCMnnEA.png)
    for png in sorted((output.parent / 'previews').glob('SCM*.png')):
        arts['preview-' + png.stem] = Image.open(png).convert('RGBA')
    for name, im in sorted(arts.items()):
        im = im.convert('RGBA'); data = im.tobytes(); key = name.encode('ascii'); assert len(key) < 64
        records.append((key, im.width, im.height, data))
        proofs[name] = {'rgba_sha256': hashlib.sha256(data).hexdigest(), 'width': im.width, 'height': im.height}
    blob = bytearray(MAGIC + struct.pack('<I', len(records)))
    for key, w, h, data in records:
        blob += key.ljust(64, b'\0') + struct.pack('<III', w, h, len(data)) + data
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(blob)
    output.with_suffix('.json').write_text(json.dumps({'design': 'ra-enhanced-menu-v01', 'assets': proofs,
        'pack_sha256': hashlib.sha256(blob).hexdigest()}, indent=1) + '\n')
    print('Red Alert Enhanced menu: %d assets, %s bytes -> %s' % (len(records), format(len(blob), ','), output))

if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0]); ap.add_argument('output', type=Path)
    bake(ap.parse_args().output)
