"""Review sheets beside the screen mockups: the kit, the open choices, the emblem concepts
and an index of every screen."""
import os, sys, glob
import numpy as np
from PIL import Image
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import paths
import chassis as C, screens as S, compose as K, fontrender as F, ecur

OUT = sys.argv[1] if len(sys.argv) > 1 else paths.OUT; MOCK = sys.argv[2] if len(sys.argv) > 2 else OUT
SRC = paths.WORK
EMB = paths.EMBLEMS + '/'
def label(img, s, x, y, col=(230, 222, 206), scale=2):
    F.draw(img, s.upper(), x, y, col, scale=scale)
def up(im, k): return im.resize((im.width * k, im.height * k), Image.NEAREST)

# ---- the kit --------------------------------------------------------------------------
import dialogbox_1996 as D
kit = Image.new('RGBA', (2560, 2000), (24, 24, 26, 255))
label(kit, 'Red Alert Enhanced chassis: the 1996 pieces and what is built from them (native pixels drawn 2x)', 40, 30)
label(kit, '1996: Dialog_Box at the title (title palette) and in a mission (scenario palette)', 40, 90, (196, 160, 150))
for i, th in enumerate(('red', 'blue')):
    d = D.dialog_box(400, 240, th); kit.alpha_composite(up(d, 2), (40 + i * 840, 130))
label(kit, 'Enhanced: the same rails, rivets, ribbing and grid at any size, the field darkened to 62%, TD Enhanced controls', 40, 650, (196, 160, 150))
y0 = 760
for i, th in enumerate(('red', 'blue')):
    T = C.THEMES[th]; x0 = 40 + i * 1260
    panel = C.frame(600, 600, T)
    for k, st in enumerate(['normal', 'hover', 'active', 'activehover', 'pressed', 'disabled']):
        b = C.button(240, 40, st, T); panel.alpha_composite(b, (40, 40 + k * 48))
        C.text(panel, st, 160, 52 + k * 48, T.muted if st == 'disabled' else T.text, center=True)
    for k, st in enumerate(['normal', 'hover', 'active']):
        panel.alpha_composite(C.button(28, 28, st, T), (300 + k * 36, 46))
    C.well(panel, 'WELL TITLE', 300, 90, 270, 200, T)
    C.checkbox(panel, 318, 150, True, False, T); C.text(panel, 'On', 352, 153, T.text)
    C.checkbox(panel, 318, 190, False, False, T); C.text(panel, 'Off', 352, 193, T.text)
    C.checkbox(panel, 318, 230, True, True, T); C.text(panel, 'Disabled', 352, 233, T.muted)
    v = S.View('main', '', th); v.img = panel; v.seats = []
    v.add('Range', 40, 340, 520, 36, S.RANGE, value=6, max=10)
    v.add('Field text', 40, 400, 250, 40, S.FIELD, caret=True)
    v.add('Selected row', 300, 400, 260, 32, S.ROW, active=True)
    v.add('Hovered row', 300, 436, 260, 32, S.ROW, hover=True)
    v.add('Dropdown', 40, 460, 250, drop=True)
    for c in range(8): v.add('', 40 + c * 32, 520, 28, 28, S.COLOUR, colour=S.COLOURS[c], active=c == 0)
    S.paint(v)
    pl, _ = C.piece('BTN-PL', 0, T); st_, _ = C.piece('BTN-ST', 0, T); upa, _ = C.piece('BTN-UP', 0, T); dn, _ = C.piece('BTN-DN', 0, T)
    for k, ic in enumerate((pl, st_, upa, dn)): panel.alpha_composite(ic, (320 + k * 34, 524))
    kit.alpha_composite(up(panel, 2), (x0, y0))
    label(kit, ('Title (red)' if th == 'red' else 'In a mission (blue)'), x0, y0 - 40, (231, 167, 0))
kit.convert('RGB').save(os.path.join(OUT, '50-kit-chassis.png'), optimize=True)

# ---- open choice: the pointer ---------------------------------------------------------
v = S.main(hover='Skirmish')
a, _ = K.compose(v, 'title', title=True, main=True, pointer=(320, 318))
b, (s, x, y) = K.compose(v, 'title', title=True, main=True)
b = b.convert('RGBA'); m = Image.open(os.path.join(SRC, 'MOUSE_HIRES_00.png')).convert('RGBA')
b.alpha_composite(up(m, s), (x + 320 * s, y + 318 * s))
crop = (1000, 640, 1800, 1000)
sheet = Image.new('RGB', (1640, 460), (24, 24, 26))
sheet.paste(a.crop(crop), (10, 60)); sheet.paste(b.convert('RGB').crop(crop), (830, 60))
label(sheet, 'A (proposed): the shared Enhanced pointer', 10, 16); label(sheet, 'B: Red Alert\'s own pointer, 2x', 830, 16)
sheet.save(os.path.join(OUT, '52-choice-pointer.png'), optimize=True)

# ---- open choice: the lettering -------------------------------------------------------
orig = K.BACKDROP; K.BACKDROP = Image.open(os.path.join(paths.ROOT, 'menu', 'art', 'ra3d-title-win95.png')).convert('RGB')
wh, wl = K.WORD_HI, K.WORD_LO; K.WORD_HI = Image.new('RGBA', (1, 1)); K.WORD_LO = Image.new('RGBA', (1, 1))
whole, _ = K.compose(v, 'title', title=True, main=True)
K.BACKDROP = orig; K.WORD_HI, K.WORD_LO = wh, wl
split, _ = K.compose(v, 'title', title=True, main=True)
sheet = Image.new('RGB', (2580, 820), (24, 24, 26))
sheet.paste(split.resize((1280, 720), Image.LANCZOS), (10, 80)); sheet.paste(whole.resize((1280, 720), Image.LANCZOS), (1300, 80))
label(sheet, 'A (proposed): lettering lifted off the title and set above the panel', 10, 20)
label(sheet, 'B: the title left whole: the panel covers the lettering', 1300, 20)
sheet.save(os.path.join(OUT, '53-choice-lettering.png'), optimize=True)

# ---- open choice: the font --------------------------------------------------------------
fa = S.main(); fb_ = C.frame(572, 564, C.THEMES['red'])
C.text(fb_, 'MAIN MENU', 286, 32, C.THEMES['red'].text, center=True)
labels = ['Start New Game', 'Special Ops', 'User Maps', 'Load Mission']
for k, lab in enumerate(labels):
    fb_.alpha_composite(C.button(500, 40, 'active' if k == 0 else 'normal', C.THEMES['red']), (36, 64 + k * 44))
    F.draw(fb_, lab, 286, 64 + k * 44 + 14, C.THEMES['red'].text, res='HIRES', scale=1, center=True)
sheet = Image.new('RGB', (2340, 520), (24, 24, 26))
sheet.paste(up(fa.img.crop((0, 0, 572, 236)), 2).convert('RGB'), (10, 60)); sheet.paste(up(fb_.crop((0, 0, 572, 236)), 2).convert('RGB'), (1180, 60))
label(sheet, 'A (proposed): GRAD6FNT, the LORES face drawn 2x (as TD Enhanced)', 10, 16)
label(sheet, 'B: GRAD6FNT, the Win95 HIRES face at 1x (mixed case)', 1180, 16)
sheet.save(os.path.join(OUT, '54-choice-font.png'), optimize=True)

# ---- open choice: an Enhanced side choice ---------------------------------------------
v = S.View('settings', 'CHOOSE YOUR SIDE', 'red')
v.well('CAMPAIGN', 38, 110, 600, 350)
v.badges.append(('Allied', 106, 166, 136, 'plate')); v.badges.append(('Soviet', 434, 166, 136, 'plate'))
v.add('Allies', 56, 312, 268, active=True); v.add('Soviet', 352, 312, 268)
v.text('Difficulty', 56, 374)
for k, d in enumerate(['Easy', 'Normal', 'Hard']): v.add(d, 56 + k * 192, 398, 180, active=d == 'Normal')
v.add('Back', 38, 498, 192); v.add('Start', 446, 498, 192, active=True)
S.paint(v)
img, _ = K.compose(v, 'title', pointer=(180, 330)); img.save(os.path.join(OUT, '55-choice-side-enhanced-1440.png'), optimize=True)

# ---- the emblem concepts --------------------------------------------------------------
sheet = Image.new('RGB', (2560, 1500), (24, 24, 26))
label(sheet, 'Emblem concepts (concept step), 1996 references on the left; the menu would show the 3D model turning', 30, 20)
refs_a = [Image.open(EMB + 'allied/c1/1_ref_1996_radar_logo.png'), Image.open(EMB + 'allied/c1/2_ref_1996_logo_movie.png')]
refs_s = [Image.open(EMB + 'soviet/c1/1_ref_1996_radar_logo.png'), Image.open(EMB + 'soviet/c1/2_ref_1996_logo_movie.png')]
for row, (refs, concepts, names) in enumerate([(refs_a, [EMB + 'allied/c1/concept.png', EMB + 'allied/c1g/concept.png'], ['Allied A: the 1996 plaque', 'Allied B: with a globe']),
                                               (refs_s, [EMB + 'soviet/c1/concept.png'], ['Soviet: the hammer and sickle star'])]):
    y = 80 + row * 720
    r0 = refs[0].convert('RGB').resize((400, 353)); sheet.paste(r0, (30, y + 40))
    r1 = refs[1].convert('RGB').resize((400, 98)); sheet.paste(r1, (30, y + 410))
    label(sheet, '1996 radar logo / logo movie', 30, y, (196, 160, 150), 1)
    for k, cp in enumerate(concepts):
        c = Image.open(cp).convert('RGB').resize((640, 640), Image.LANCZOS); sheet.paste(c, (470 + k * 680, y + 40)); label(sheet, names[k], 470 + k * 680, y, (231, 167, 0))
    # how it would sit in the main menu corner, at its runtime size
    e = Image.open(os.path.join(SRC, 'emblem_%s.png' % ('Allied' if row == 0 else 'Soviet'))).convert('RGBA').resize((344, 344), Image.LANCZOS)
    base = Image.new('RGBA', (400, 400), (0, 0, 0, 255)); base.alpha_composite(e, (28, 28)); sheet.paste(base.convert('RGB'), (2130, y + 40))
    label(sheet, 'at menu size (1440p)', 2130, y, (196, 160, 150), 1)
sheet.save(os.path.join(OUT, '60-emblem-concepts.png'), optimize=True)

# ---- index ----------------------------------------------------------------------------
files = sorted(f for f in glob.glob(os.path.join(MOCK, '*.png')) if not f.endswith('00-index.png'))
cols = 6; tw, th = 420, 236; rows = (len(files) + cols - 1) // cols
idx = Image.new('RGB', (cols * (tw + 10) + 10, rows * (th + 40) + 70), (24, 24, 26))
label(idx, 'Red Alert Enhanced menus v01: every screen (2560x1440 unless named)', 10, 16)
for i, f in enumerate(files):
    im = Image.open(f).convert('RGB'); im.thumbnail((tw, th))
    x = 10 + (i % cols) * (tw + 10); y = 60 + (i // cols) * (th + 40)
    idx.paste(im, (x, y)); label(idx, os.path.basename(f)[:-4][:40], x, y + th + 6, (196, 160, 150), 1)
idx.save(os.path.join(OUT, '00-index.png'), optimize=True)
print('sheets done')
