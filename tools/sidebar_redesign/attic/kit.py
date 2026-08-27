"""Shared rig for the DOS sidebar re-skin.
Geometry is LOCKED: every position below was verified against the real render in
sidebar/dossidebar_preview.png, not guessed. dosbar.h agrees except for a 1px strip
inset (see the factor +1/-1 note at dosbar.h:65)."""
from PIL import Image
import numpy as np, os
from collections import deque

HERE = os.path.dirname(os.path.abspath(__file__))
ART  = os.path.join(HERE, "art")
REF  = os.path.join(HERE, "ref_native.png")

W, H       = 80, 200
TABS_Y     = 0;  TABS_H  = 7
SEP_Y      = 7
RADAR_Y    = 8;  RADAR_H = 69
BTN_Y      = 80; BTN_H   = 9
BTN_BOXES  = [(2, 33, "REPAIR"), (36, 55, "SELL"), (58, 77, "MAP")]
STRIP_Y0   = 91
CELL_W, CELL_H = 32, 24
COL_X      = [9, 44]
ROW_Y      = [91, 115, 139, 163]
ARROW_Y    = 188
ARROW_XS   = [(10, "up"), (26, "dn"), (45, "up"), (61, "dn")]
METER_H    = 187 - STRIP_Y0

CAMEOS_L = ["FACT", "NUKE", "PROC", "WEAP"]
CAMEOS_R = ["E1", "E2", "JEEP", "MTNK"]

# ---------------------------------------------------------------- palette
def full_palette():
    """The game's real 256-entry palette, read out of dossidebar.pack."""
    return np.load(os.path.join(HERE, "palette256.npy"))

_PAL = None
def snap(img):
    """Nearest-colour snap to the true palette. Alpha is preserved untouched."""
    global _PAL
    if _PAL is None:
        p = full_palette()
        _PAL = np.unique(p[1:], axis=0)        # index 0 is transparent, never pick it
    a = np.array(img.convert("RGBA")).astype(int)
    rgb, al = a[:, :, :3], a[:, :, 3]
    flat = rgb.reshape(-1, 3)
    d = ((flat[:, None, :] - _PAL[None, :, :]) ** 2).sum(axis=2)
    out = _PAL[d.argmin(axis=1)].reshape(rgb.shape)
    return Image.fromarray(np.dstack([out, al]).astype(np.uint8), "RGBA")

# ---------------------------------------------------------------- font, lifted from the render
def _masks():
    nat = np.array(Image.open(REF).convert("RGB")).astype(int)
    def m(y0, y1, x0, x1, pred):
        return np.array([[pred(nat[y, x]) for x in range(x0, x1)] for y in range(y0, y1)])
    light = lambda c: c[0] >= 0xd8 and c[1] >= 0xd8 and c[2] >= 0xd8
    sidebar = m(1, 6, 20, 61, light)
    row = m(82, 87, 0, 80, light)
    labels = {}
    for x0, x1, name in BTN_BOXES:
        sub = row[:, x0:x1 + 1]
        cols = [c for c in np.where(sub.any(axis=0))[0] if c > 1]
        labels[name] = (sub[:, cols[0]:cols[-1] + 1], x0 + cols[0])
    return sidebar, labels

SIDEBAR_TXT, BTN_LABELS = _masks()

def stamp(img, mask, x, y, colour, shadow=None):
    px = img.load(); h, w = mask.shape
    if shadow:
        for j in range(h):
            for i in range(w):
                if mask[j, i] and 0 <= x+i+1 < img.width and 0 <= y+j+1 < img.height:
                    px[x+i+1, y+j+1] = shadow
    for j in range(h):
        for i in range(w):
            if mask[j, i] and 0 <= x+i < img.width and 0 <= y+j < img.height:
                px[x+i, y+j] = colour

# ---------------------------------------------------------------- art access
def frames(name, fw, fh):
    im = Image.open(f"{ART}/{name}.png").convert("RGBA")
    return [im.crop((i*fw, 0, (i+1)*fw, fh)) for i in range(im.width // fw)]

def cameo(name):
    return Image.open(f"{ART}/{name}.png").convert("RGBA").crop((0, 0, CELL_W, CELL_H))

def emblem(faction="GDI"):
    """Lift the painted medallion off its flat field by flood-filling from the border."""
    src = Image.open(f"{ART}/RADAR{faction}.png").convert("RGB").crop((0, 0, 80, 69))
    a = np.array(src).astype(int); h, w = a.shape[:2]
    seen = np.zeros((h, w), bool); bg = np.zeros((h, w), bool)
    tgt = a[0, 0]
    q = deque([(y, x) for x in range(w) for y in (0, h-1)] +
              [(y, x) for y in range(h) for x in (0, w-1)])
    while q:
        y, x = q.popleft()
        if seen[y, x]: continue
        seen[y, x] = True
        if abs(a[y, x] - tgt).sum() > 30: continue
        bg[y, x] = True
        for dy, dx in ((1,0),(-1,0),(0,1),(0,-1)):
            ny, nx = y+dy, x+dx
            if 0 <= ny < h and 0 <= nx < w and not seen[ny, nx]: q.append((ny, nx))
    out = Image.new("RGBA", (w, h), (0,0,0,0)); o = out.load()
    for y in range(h):
        for x in range(w):
            if not bg[y, x]: o[x, y] = tuple(int(v) for v in a[y, x]) + (255,)
    return out

def assemble_original(faction="GDI"):
    ref = Image.open(REF).convert("RGBA")
    im = Image.new("RGBA", (W, H), (0xa8, 0xa8, 0xa8, 255))
    im.alpha_composite(Image.open(f"{ART}/TABS.png").convert("RGBA"), (0, 0))
    stamp(im, SIDEBAR_TXT, 20, 1, (0xfc,)*3 + (255,), (0x54,)*3 + (255,))
    px = im.load()
    for x in range(W): px[x, SEP_Y] = (0, 0, 0, 255)
    im.alpha_composite(frames("RADAR"+faction, 80, 69)[0], (0, RADAR_Y))
    im.alpha_composite(ref.crop((0, BTN_Y, 80, BTN_Y+BTN_H)), (0, BTN_Y))
    im.alpha_composite(ref.crop((0, STRIP_Y0, 9, 187)), (0, STRIP_Y0))
    well, frame = frames("STRIP", 32, 24)
    for col, names in zip(COL_X, (CAMEOS_L, CAMEOS_R)):
        for row, nm in zip(ROW_Y, names):
            im.alpha_composite(well, (col, row))
            im.alpha_composite(cameo(nm), (col, row))
            im.alpha_composite(frame, (col, row))
    up, dn = frames("STRIPUP",16,12)[0], frames("STRIPDN",16,12)[0]
    for x, d in ARROW_XS: im.alpha_composite(up if d=="up" else dn, (x, ARROW_Y))
    return im

def upscale(im, s): return im.resize((im.width*s, im.height*s), Image.NEAREST)
