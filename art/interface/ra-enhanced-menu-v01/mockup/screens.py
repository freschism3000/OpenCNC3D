"""Every Enhanced menu screen, laid out exactly as menu/enhanced_menu.cpp lays the Tiberian
Dawn ones (same seats, same native coordinates), drawn in the Red Alert chassis with Red
Alert's content. Each function returns (panel RGBA, info) where info carries the frame
kind and any emblems to draw over the panel."""
import os
import numpy as np
from PIL import Image
import paths
import chassis as C

BUTTON, CHECK, RANGE, FIELD, ROW, COLOUR = range(6)
FRAMES = {'main': (572, 564), 'pause': (708, 528), 'settings': (676, 584), 'slots': (676, 528),
          'visuals': (1016, 636), 'confirm': (676, 340), 'lobby': (1216, 636)}

class Seat:
    def __init__(s, label, x, y, w, h=40, kind=BUTTON, **k):
        s.label, s.x, s.y, s.w, s.h, s.kind = label, x, y, w, h, kind
        s.active = k.get('active', False); s.disabled = k.get('disabled', False)
        s.value = k.get('value', 0); s.min = k.get('min', 0); s.max = k.get('max', 1)
        s.drop = k.get('drop', False); s.badge = k.get('badge'); s.hover = k.get('hover', False)
        s.pressed = k.get('pressed', False); s.colour = k.get('colour'); s.caret = k.get('caret', False)
        s.icon = k.get('icon')

class View:
    def __init__(v, frame, title, theme, compact=False):
        v.theme = C.THEMES[theme]; v.kind = frame
        w, h = FRAMES[frame]
        if compact:            # dialogs opened from the title: the main frame's height
            h = FRAMES['main'][1]
        v.w, v.h = w, h
        v.img = C.frame(w, h, v.theme); v.seats = []; v.badges = []; v.popup = None; v.popup_hint = None
        C.text(v.img, title, w // 2, 32, v.theme.text, center=True)
    def add(v, *a, **k):
        s = Seat(*a, **k); v.seats.append(s); return s
    def text(v, s, x, y, col=None, center=False):
        C.text(v.img, s, x, y, col or v.theme.text, center)
    def muted(v, s, x, y, center=False):
        C.text(v.img, s, x, y, v.theme.muted, center)
    def accent(v, s, x, y, center=False):
        C.text(v.img, s, x, y, v.theme.accent, center)
    def well(v, title, x, y, w, h, overlay=False):
        C.well(v.img, title, x, y, w, h, v.theme, overlay)
    def note(v, s, x, y, width, maxlines=99, col=None, pitch=26, newest=False):
        C.note(v.img, s, x, y, width, maxlines, col, pitch, v.theme, newest)

def paint(v):
    T = v.theme; img = v.img
    for n, q in enumerate(v.seats):
        if v.popup and n == v.popup[0]:
            px, py, pw, ph = v.popup[1]; C.well(img, '', px, py, pw, ph, T, overlay=True)
        col = T.muted if q.disabled else T.text
        if q.kind == FIELD:
            C.rect(img, q.x, q.y, q.w, q.h, (6, 3, 3))
            C.rect(img, q.x, q.y, q.w, 1, T.accent if q.caret else (128, 120, 112)); C.rect(img, q.x, q.y, 1, q.h, (128, 120, 112))
            C.rect(img, q.x, q.y + q.h - 1, q.w, 1, (52, 40, 38)); C.rect(img, q.x + q.w - 1, q.y, 1, q.h, (52, 40, 38))
            C.text(img, C.fitted(q.label, q.w - 24), q.x + 12, q.y + 12, col)
            if q.caret: C.rect(img, min(q.x + 12 + C.textwidth(q.label), q.x + q.w - 14), q.y + 10, 1, 19, T.accent)
            continue
        if q.kind == ROW:
            if q.active or q.hover:
                C.rect(img, q.x, q.y, q.w, q.h, T.row_on if q.active else T.row_hover); C.rect(img, q.x, q.y, 2, q.h, T.accent)
            left = q.x + (32 if q.colour is not None else 12 if q.badge is None else 44)
            if q.badge is not None: v.badges.append((q.badge, q.x + 6, q.y + 2, 28))
            C.text(img, C.fitted(q.label, q.w - (24 if q.badge is None else 56)), left, q.y + (q.h - 16) // 2, col)
            continue
        if q.kind == CHECK:
            if q.hover and not q.disabled: C.rect(img, q.x, q.y, q.w, q.h, T.row_hover)
            C.checkbox(img, q.x + 2, q.y + 7, q.value, q.disabled, T)
            C.text(img, C.fitted(q.label, q.w - 44), q.x + 38, q.y + 10, col); continue
        if q.kind == RANGE:
            fill = int((q.w - 28) * (q.value - q.min) / max(1, q.max - q.min)); x, y, w = q.x + 14, q.y + 14, q.w - 28
            C.rect(img, x - 2, y - 2, w + 4, 12, (6, 2, 2)); C.rect(img, x, y + 9, w, 1, (90, 60, 56)); C.rect(img, x + w + 1, y, 1, 9, (90, 60, 56))
            C.rect(img, x, y, w, 8, (30, 6, 4) if T.name == 'red' else (8, 16, 40))
            C.rect(img, x, y, fill, 8, (90, 70, 66) if q.disabled else (T.ramp[2] if q.hover else T.ramp[4]))
            C.rect(img, x, y, fill, 1, T.accent if not q.disabled else (150, 140, 130))
            b = C.button(28, 28, 'hover' if q.hover else 'normal', T); img.alpha_composite(b, (q.x + fill, q.y + 4)); continue
        state = 'disabled' if q.disabled else 'pressed' if q.pressed else ('activehover' if q.active else 'hover') if q.hover else 'active' if q.active else 'normal'
        img.alpha_composite(C.button(q.w, q.h, state, T), (q.x, q.y))
        if q.kind == COLOUR:
            C.rect(img, q.x + 7, q.y + 7, 14, 14, q.colour); continue
        if q.icon is not None:
            ic = q.icon; img.alpha_composite(ic, (q.x + 18, q.y + (q.h - ic.height) // 2))
        if q.badge is not None: v.badges.append((q.badge, q.x + 6, q.y + 6, 28))
        lab = C.fitted(q.label, q.w - (52 if q.badge is not None else 16))
        C.text(img, lab, q.x + q.w // 2 + (14 if q.badge is not None else 0) - (8 if q.drop else 0), q.y + (q.h - 16) // 2 + (1 if q.pressed else 0), col, center=True)
        if q.drop:
            for i in range(5): C.rect(img, q.x + q.w - 27 + i, q.y + 17 + i, 9 - 2 * i, 1, col)
    if v.popup_hint and v.popup:
        px, py, pw, ph = v.popup[1]; C.text(img, v.popup_hint, px + 16, py + ph - 26, T.muted)
    return v

# ---- Red Alert content ---------------------------------------------------------------
VERSION = 'OPENCNC3D V0.8.7'
COUNTRIES = ['England', 'Germany', 'France', 'Spain', 'Greece', 'Turkey', 'USSR', 'Ukraine']
def _ra_colours():
    from ramix import RaMix
    from bake_dosmenu import load_cps
    D = paths.RADATA + '/'
    loc = RaMix(D + 'LOCAL.MIX'); px, _ = load_cps(loc.read('PALETTE.CPS'))
    P = C.PAL['blue']
    # PALETTE.CPS rows 0..7: GOLD LTBLUE RED GREEN ORANGE GREY BLUE(teal) BROWN, each
    # remap's column 3 (the unit paint's lit tone) as the swatch.
    return [P[px[r * 320 + 3]] for r in range(8)]
COLOURS = _ra_colours()

# ---- screens --------------------------------------------------------------------------
def main(hover=None, selected='Start New Game', special_installed=False):
    v = View('main', 'MAIN MENU', 'red')
    order = [('Start New Game', 64), ('Special Ops', 108), ('User Maps', 152), ('Load Mission', 196),
             ('Multiplayer', 260), ('Skirmish', 304), ('Settings', 368), ('Intro & Sneak Peek', 412), ('Exit Game', 456)]
    for lab, y in order:
        v.add(lab, 36, y, 500, active=lab == selected, hover=lab == hover, disabled=(lab == 'Special Ops' and not special_installed))
    for y in (246, 354):
        C.rect(v.img, 38, y, 496, 2, (6, 2, 2)); C.rect(v.img, 38, y + 2, 496, 1, v.theme.well_lit); C.rect(v.img, 38, y + 3, 496, 1, (20, 4, 2))
    v.accent('ENHANCED / ' + VERSION, v.w // 2, 516, center=True)
    return paint(v)

def choice_exit():
    v = View('confirm', 'EXIT GAME', 'red')
    v.text('Exit Red Alert 3D?', 338, 136, center=True)
    v.add('Cancel', 98, 238, 220); v.add('Exit Game', 358, 238, 220, active=True)
    return paint(v)

def choice_intro(still):
    v = View('settings', 'INTRO & SNEAK PEEK', 'red')
    v.well('MOVIES', 38, 110, 600, 350)
    # Drawn at the size the well gives it (320 x 156, the movie's own), not unscaled.
    v.img.alpha_composite(still.convert('RGBA'), (178, 174))
    v.add('Play Intro', 56, 382, 564, active=True); v.add('Back', 38, 498, 192)
    return paint(v)

def pause(theme='blue', hover=None):
    v = View('pause', 'OPTIONS', theme)
    v.accent('MISSION PAUSED', 354, 64, center=True); v.muted('MISSION', 36, 110); v.muted('SETTINGS', 362, 110)
    for i, (lab, dis) in enumerate([('Load Mission', 0), ('Save Mission', 0), ('Delete Mission', 0), ('Restate', 0)]):
        v.add(lab, 36, 136 + i * 48, 310, disabled=dis, hover=lab == hover)
    for i, lab in enumerate(['Game Controls', 'Visuals', 'Gameplay']):
        v.add(lab, 362, 136 + i * 48, 310, hover=lab == hover)
    v.add('Resume Mission', 36, 410, 204, active=True); v.add('Abort Mission', 252, 410, 204); v.add('Exit Game', 468, 410, 204)
    return paint(v)

def controls(theme='blue', compact=False):
    v = View('visuals', 'GAME CONTROLS', theme, compact)
    v.well('GAMEPLAY', 38, 110, 460, v.h - 228); v.well('AUDIO', 518, 110, 460, v.h - 228)
    def rng(label, x, y, w, val, mx, pct):
        v.text(label, x, y - 28); s = str(val * 100 // mx) + '%' if pct else ('Normal' if val == 4 else str(val + 1))
        v.accent(s, x + w - C.textwidth(s), y - 28); v.add(label, x, y, w, 36, RANGE, value=val, max=mx)
    c = compact
    rng('Game speed', 56, 188 if c else 216, 424, 4, 6, False); rng('Scroll rate', 56, 310 if c else 354, 424, 3, 6, False)
    for (yy) in ((236 if c else 264), (358 if c else 402)):
        v.muted('Slower', 56, yy); v.muted('Faster', 414, yy)
    rng('Music volume', 536, 188 if c else 216, 424, 167, 239, True); rng('Sound volume', 536, 276 if c else 316, 424, 215, 239, True)
    rng('Speech volume', 536, 364 if c else 416, 424, 239, 239, True)
    v.add('Sound Controls', 56, 402 if c else 462, 424)
    v.add('Reset to Defaults', 38, v.h - 86, 292); v.add('Back', 550, v.h - 86, 192); v.add('OK', 758, v.h - 86, 220, active=True)
    return paint(v)

def gameplay(theme='blue'):
    v = View('visuals', 'GAMEPLAY', theme)
    v.well('INPUT', 38, 110, 460, v.h - 228); v.well('FEEDBACK', 518, 110, 460, v.h - 228)
    v.add('Swap mouse buttons', 56, 202, 424, 36, CHECK, value=0); v.add('Right button scrolls', 56, 266, 424, 36, CHECK, value=1, hover=True)
    v.add('Credit tick sound', 536, 202, 424, 36, CHECK, value=1)
    v.add('Back', 38, v.h - 86, 192); v.add('OK', 758, v.h - 86, 220, active=True)
    return paint(v)

def visuals(section, theme='blue', drop=None, compact=False):
    v = View('visuals', 'VISUALS', theme, compact)
    cats = ['Display', 'World', 'Effects', 'Interface']
    for i, c in enumerate(cats): v.add(c, 38 + i * 238, 86, 230, active=i == section)
    shrink = 72 if compact else 0; pend = None
    modes = ['Classic', 'N64', 'Enhanced']
    def check(lab, x, y, on, dis=False): v.add(lab, x, y, 424, 36, CHECK, value=on, disabled=dis)
    def dropdown(lab, val, x, y, dis=False):
        v.text(lab, x, y - 26, v.theme.muted if dis else None); return v.add(val, x, y, 424, drop=True, disabled=dis)
    if section == 0:
        v.well('DISPLAY', 38, 160, 460, 358 - shrink); v.well('GRAPHICS MODE', 518, 160, 460, 180); v.well('OUTPUT', 518, 360, 460, 158 - shrink)
        for i, d in enumerate(['Fullscreen', 'Windowed', 'Borderless']): v.add(d, 54 + i * 144, 208, 140, active=i == 2)
        res = dropdown('Resolution', '2560 X 1440', 56, 272 if compact else 294)
        check('Vertical sync', 56, 330 if compact else 356, 1); check('FPS counter', 56, 382 if compact else 408, 0)
        v.muted('Current: 2560 X 1440', 56, 426 if compact else 476)
        for i, m in enumerate(modes): v.add(m, 534 + i * 144, 208, 140, active=i == 2, disabled=i == 1)
        v.muted('Enhanced presentation', 536, 278)
        check('Console output gamma', 536, 406, 0)
        if drop == 'res':
            opts = ['3840 X 2160', '2560 X 1600', '2560 X 1440', '1920 X 1200', '1920 X 1080', '1600 X 900', '1280 X 720']
            pend = (res, opts, '2560 X 1440', '1920 X 1080', 'Mouse wheel: more resolutions')
    elif section == 1:
        v.well('SURFACES', 38, 160, 460, 358 - shrink); v.well('OBJECTS', 518, 160, 460, 358 - shrink)
        tx = dropdown('Terrain', 'Enhanced Textures', 56, 244); check('Water Shader', 56, 314, 1); check('Bilinear filtering', 56, 366, 0)
        dropdown('Infantry', '3D Soldiers', 536, 244); check('3D Trees', 536, 314, 1)
        check('3D Ore', 536, 366, 0, dis=True)
        v.note('3D Ore and gems are not drawn in Red Alert yet', 536, 410, 424, 2)
        if drop == 'tex':
            pend = (tx, ['DOS Textures', 'Enhanced Textures'], 'Enhanced Textures', None, None)
    elif section == 2:
        v.well('LIGHTING', 38, 160, 460, 358 - shrink); v.well('POST PROCESSING', 518, 160, 460, 358 - shrink)
        for i, (a, b) in enumerate([('Sun shadows', 'Bloom'), ('Ambient occlusion', 'Colour grade'), ('Dynamic light', 'CRT')]):
            check(a, 56, 210 + i * 52, 1); check(b, 536, 210 + i * 52, 1 if i < 2 else 0)
    elif section == 3:
        v.well('INTERFACE', 38, 160, 460, 358 - shrink); v.well('CAMERA', 518, 160, 460, 358 - shrink)
        check('New HUD', 56, 210, 0, dis=True); check('Smooth animations', 56, 262, 1)
        dropdown('UI scaling', 'Auto', 56, 362); dropdown('Perspective', 'Isometric', 536, 244)
        v.note('The new HUD has no Red Alert art yet', 56, 452, 424, 2)
        v.muted('Classic: top-down view', 536, 326); v.muted('Isometric: angled view', 536, 358)
    else:
        v.well('WORLD DETAIL', 38, 160, 460, 358 - shrink); v.well('TERRAIN DETAIL', 518, 160, 460, 358 - shrink)
        check('Bushes', 56, 210, 0, dis=True); check('Rocks', 56, 262, 1)
        check('Ground patches', 536, 210, 0, dis=True); check('Terrain relief', 536, 262, 1)
        v.note('Greyed rows are not drawn in Red Alert yet', 56, 330, 424, 2)
    if section != 4: v.add('More Settings', 342, v.h - 86, 192)
    v.add('Reset to Defaults', 38, v.h - 86, 292); v.add('Back', 550, v.h - 86, 192); v.add('OK', 758, v.h - 86, 220, active=True)
    if pend:
        own, opts, cur, hov, hint = pend; own.active = True
        hh = 30 if hint else 0; y = own.y + 44
        if y + len(opts) * 42 + 8 + hh > v.h - 106: y = max(78, own.y - len(opts) * 42 - 12 - hh)
        v.popup = (len(v.seats), (own.x - 4, y - 4, own.w + 8, len(opts) * 42 + 8 + hh)); v.popup_hint = hint
        for i, o in enumerate(opts): v.add(o, own.x, y + i * 42, own.w, active=o == cur, hover=o == hov)
    return paint(v)

SAVES = [('Allied 1 - In the thick of it', 'Allied'), ('Allied 3 - Dead End', 'Allied'), ('Soviet 1 - Lesson in Blood', 'Soviet'),
         ('Soviet 4 - Behind The lines', 'Soviet'), ('Allied 5 - autosave', 'Allied')]
def slots(mode, theme='blue', compact=False, empty=False):
    title = {'save': 'SAVE MISSION', 'load': 'LOAD MISSION', 'delete': 'DELETE MISSION'}[mode]
    v = View('visuals', title, theme, compact)
    v.well('SAVED MISSIONS', 38, 110, 600, v.h - 228); v.well('MISSION', 658, 110, 320, v.h - 228)
    rows = 9 if mode == 'save' else 10; pitch = 24 if compact else 32
    sel = 1
    if not empty:
        for i, (t, side) in enumerate(SAVES[:rows]):
            v.add(t, 56, 164 + i * pitch, 564, pitch, ROW, active=i == sel)
        v.note(SAVES[sel][0], 676, 186, 284, 6, col=v.theme.text)
    else:
        v.muted('No saved missions', 56, 178)
    if mode == 'save':
        v.text('Mission name', 676, 374); v.add(SAVES[sel][0] if not empty else 'Allied 3 - Dead End', 676, 398, 284, 40, FIELD, caret=True)
    v.add('Back', 38, v.h - 86, 192)
    v.add({'save': 'Save', 'load': 'Load', 'delete': 'Delete'}[mode], v.w - 258, v.h - 86, 220, active=True, disabled=empty and mode != 'save')
    return paint(v)

def confirm(kind, theme='blue'):
    titles = {'abort': 'ABORT MISSION', 'delete': 'DELETE MISSION', 'exit': 'EXIT GAME', 'leave': 'LEAVE MATCH', 'surrender': 'ABORT MISSION'}
    v = View('confirm', titles[kind], theme)
    if kind == 'abort':
        v.text('Do you want to abort the mission?', 338, 136, center=True)
        v.add('Cancel', 38, 238, 192); v.add('Restart', 242, 238, 192); v.add('Abort', 446, 238, 192, active=True)
    elif kind == 'delete':
        v.text('Delete this file?', 338, 136, center=True); v.add('No', 98, 238, 220); v.add('Yes', 358, 238, 220, active=True)
    elif kind == 'exit':
        v.text('Exit Red Alert 3D?', 338, 136, center=True); v.add('Cancel', 98, 238, 220); v.add('Exit Game', 358, 238, 220, active=True)
    elif kind == 'leave':
        v.text('Do you want to leave', 338, 136, center=True); v.text('the match?', 338, 162, center=True)
        v.add('No', 98, 238, 220); v.add('Yes', 358, 238, 220, active=True)
    return paint(v)

def restate(paper, brief, theme='blue', video=True):
    v = View('settings', 'MISSION BRIEFING', theme)
    x, y, w, h = 38, 110, 600, v.h - 234
    # The faction's briefing paper (ALIPAPER / SOVPAPER, the picture the 1996 game puts
    # behind this box), at its own HIRES density, under the liner and the text.
    p = paper.convert('RGB').crop(((640 - w) // 2, (400 - h) // 2, (640 - w) // 2 + w, (400 - h) // 2 + h))
    a = np.array(p).astype(np.float32) * 0.62
    v.img.paste(Image.fromarray(a.astype(np.uint8)), (x, y))
    v.well('MISSION OBJECTIVE', x, y, w, h)
    # the liner darkened the paper again; put it back at 62%
    v.img.paste(Image.fromarray(a[44:h - 4, 4:w - 4].astype(np.uint8)), (x + 4, y + 44))
    v.note(brief, 56, 178, 564, 10, col=v.theme.text)
    if video: v.add('Video', 38, v.h - 86, 192)
    v.add('Back', 446, v.h - 86, 192, active=True)
    return paint(v)

def notice(text, caption='NOTICE', theme='red'):
    v = View('confirm', caption, theme)
    v.note(text, 98, 112, 480, 4, col=v.theme.text); v.add('OK', 228, 238, 220, active=True)
    return paint(v)

def sound(theme='blue', compact=False, play_icon=None, stop_icon=None, top=0):
    import re
    src = open(os.path.join(paths.ROOT, 'audio', 'ra_theme_table.h')).read()
    names = [m for m in re.findall(r'\{\{"\w+", 0, \d+, (\d), \d, 1, "([^"]+)"\}', src)]
    tracks = [n for flag, n in names if flag == '1']
    v = View('visuals', 'SOUND CONTROLS', theme, compact)
    v.well('MUSIC LIBRARY', 38, 110, 600, v.h - 228); v.well('AUDIO', 658, 110, 320, v.h - 228)
    pitch = 24 if compact else 32
    for i in range(9):
        if top + i < len(tracks): v.add(tracks[top + i], 56, 164 + i * pitch, 564, pitch, ROW, active=top + i == 4)
    def rng(label, x, y, val):
        v.text(label, x, y - 28); s = str(val * 100 // 239) + '%'; v.accent(s, x + 284 - C.textwidth(s), y - 28)
        v.add(label, x, y, 284, 36, RANGE, value=val, max=239)
    rng('Music volume', 676, 188 if compact else 210, 167); rng('Sound volume', 676, 276 if compact else 304, 215); rng('Ambience', 676, 364 if compact else 398, 180)
    v.add('Play', 38, v.h - 86, 192, icon=play_icon); v.add('Stop', 242, v.h - 86, 192, icon=stop_icon); v.add('Back', 758, v.h - 86, 220, active=True)
    v.add('Shuffle', 56, 396 if compact else 468, 268, 36, CHECK, value=1); v.add('Repeat', 352, 396 if compact else 468, 268, 36, CHECK, value=0)
    return paint(v), tracks

def cheats(theme='blue'):
    v = View('visuals', 'CHEATS', theme)
    v.well('SANDBOX', 38, 110, 940, v.h - 228)
    rows = ['Infinite Money', 'Instant Build', 'Unlock Tech Tree', 'Unlock Superweapons', 'Build Anywhere', 'Fog Of War', 'Invincibility']
    for i, r in enumerate(rows):
        v.add(r, 56 if i < 4 else 536, 178 + (i % 4) * 60, 424, 36, CHECK, value=1 if r == 'Fog Of War' else 0)
    v.add('Win', 38, v.h - 86, 192); v.add('Lose', 242, v.h - 86, 192); v.add('Reset', 550, v.h - 86, 192); v.add('OK', 758, v.h - 86, 220, active=True)
    return paint(v)

# skirmish / lobby
SEATS = [('Commander', 0, 'England', 1, 'Random', 'human'), ('Computer 1', 2, 'USSR', 2, 'Random', 'bot'),
         ('Computer 2', 1, 'Germany', 1, 'Random', 'bot'), ('Computer 3', 6, 'Ukraine', 2, 'Random', 'bot')]
def lobby(net=False, preview=None, mapname='Isle of Fury (6-8)', mapinfo=(8, 96, 96, 'Temperate'), rules=False, chat=None, theme='red'):
    title = 'MULTIPLAYER LOBBY' if net else 'SKIRMISH'
    v = View('lobby' if (net and not rules) else 'visuals', title, theme)
    for i, t in enumerate(['Players & Map', 'Match Rules']): v.add(t, 38 + i * 238, 86, 230, active=(i == 1) == rules)
    if net: v.add('Change Map', 514, 86, 230)
    if rules:
        v.well('STARTING CONDITIONS', 38, 150, 460, 368); v.well('MATCH RULES', 518, 150, 460, 368)
        for (lab, y, val, lo, hi) in [('Tech Level', 242, 10, 1, 10), ('Credits', 344, 10000, 0, 10000), ('Unit Count', 446, 0, 0, 10)]:
            v.text(lab, 56, y - 28); s = str(val); v.accent(s, 56 + 424 - C.textwidth(s), y - 28)
            v.add(lab, 56, y, 424, 36, RANGE, value=val, min=lo, max=hi)
        for i, (lab, on) in enumerate([('Bases', 1), ('Ore Regenerates', 1), ('Crates', 1), ('Shroud Regrows', 0), ('Capture The Flag', 0), ('AI Takeover', 0)]):
            v.add(lab, 536, 202 + i * 48, 424, 36, CHECK, value=on)
        v.add('Back', 38, 550, 192); v.add('Done', 758, 550, 220, active=True)
        return paint(v)
    v.well('PLAYERS', 38, 150, 392 if net else 600, 368); v.well('MAP', 450 if net else 658, 150, 188 if net else 320, 368)
    v.muted('Player', 56, 194); v.muted('Side / Team' if net else 'Side / Team / Start', 206 if net else 340, 194)
    seats = SEATS if not net else [('Commander', 0, 'England', 1, 'Random', 'human'), ('Natasha', 2, 'USSR', 2, '3', 'human'),
                                   ('Open', 1, '', 0, '', 'open'), ('Open', 3, '', 0, '', 'open')]
    summaries = []
    for i, (name, col, ctry, team, start, mode) in enumerate(seats):
        # the colour square stands where TD pads the name with spaces: RA's space is wider
        v.add(C.fitted(name, 132 if net else 228), 56, 222 + i * 32, 356 if net else 564, 30, ROW, hover=i == 1, colour=col)
        # the network column is 176 wide: a country and a team fit, the start lives in Player Setup
        summaries.append(('' if mode == 'open' else ('%s / %d' % (ctry, team) if net else '%s / %d / %s' % (ctry, team, start)), col))
    if preview is not None:
        size = 120 if net else 240
        pv = preview.resize((size, size), Image.LANCZOS).convert('RGBA')
        v.img.alpha_composite(pv, (484, 204) if net else (698, 202))
    if net:
        v.note(mapname, 468, 348, 152, 2); v.muted('%d players' % mapinfo[0], 468, 412); v.muted('Starts 1 - %d' % mapinfo[0], 468, 444); v.muted('%d X %d' % (mapinfo[1], mapinfo[2]), 468, 476)
        v.well('CHAT', 658, 150, 520, 368)
        v.note('\n'.join(chat or []), 676, 194, 484, 11, col=v.theme.text, pitch=24, newest=True)
        v.add('Good luck', 676, 466, 392, 40, FIELD, caret=True); v.add('Send', 1080, 466, 80)
    else:
        v.text(C.fitted(mapname, 284), 676, 460, v.theme.muted); v.muted('%d players / %d X %d' % mapinfo[:3], 676, 486)
        v.add('Change Map', 550, 550, 192)
    v.add('Leave' if net else 'Back', 38, 550, 192)
    v.add('Start Game' if net else 'Play', 958 if net else 758, 550, 220, active=True)
    paint(v)
    for i, (s, col) in enumerate(summaries):
        C.rect(v.img, 64, 231 + i * 32, 12, 12, COLOURS[col])
        C.text(v.img, C.fitted(s, 176 if net else 264), 219 if net else 315, 229 + i * 32, v.theme.muted)
    if net: C.text(v.img, 'Hosting on LAN', v.w - 38 - C.textwidth('Hosting on LAN'), 62, v.theme.accent)
    return v

def seat_popup(drop=None, theme='red'):
    v = View('settings', 'PLAYER SETUP', theme)
    v.well('PLAYER 2', 38, 102, 600, 356)
    def dd(label, value, x, y, w):
        v.text(label, x, y - 24); return v.add(value, x, y, w, drop=True, active=drop == label)
    st = dd('Seat type', 'Computer', 56, 178, 564); ct = dd('Country', 'USSR', 56, 270, 268)
    v.text('Player colour', 352, 246)
    for i in range(8): v.add('', 352 + i * 32, 270, 28, 28, COLOUR, colour=COLOURS[i], active=i == 2)
    dd('Team', '2', 56, 364, 268); dd('Start position', 'Random', 352, 364, 268)
    v.add('Done', 446, 498, 192, active=drop is None); v.add('Back', 38, 498, 192)
    if drop == 'Country':
        opts = COUNTRIES; h = len(opts) * 42 + 8; y = 270 + 44
        if y + h > 486: y = max(78, 270 - h - 4)
        v.popup = (len(v.seats), (56 - 4, y - 4, 268 + 8, h))
        for i, o in enumerate(opts): v.add(o, 56, y + i * 42, 268, active=o == 'USSR', hover=o == 'Ukraine')
    paint(v)
    return v

def mappicker(maps, sel, previews, theme='red', top=0):
    v = View('visuals', 'CHANGE MAP', theme)
    v.add('Official', 38, 86, 230, active=True); v.add('User Maps', 276, 86, 230)
    v.well('MAPS', 38, 150, 600, 368); v.well('DETAILS', 658, 150, 320, 368)
    for i, m in enumerate(maps[top:top + 8]):
        v.add(m[1], 56, 198 + i * 34, 564, 32, ROW, active=top + i == sel)
    v.muted('%d maps' % len(maps), 56, 490)
    if len(maps) > 8: v.add('Previous', 346, 474, 132, 32, disabled=top == 0); v.add('Next', 488, 474, 132, 32)
    m = maps[sel]
    if previews.get(m[0]) is not None:
        v.img.alpha_composite(previews[m[0]].resize((240, 240), Image.LANCZOS).convert('RGBA'), (698, 204))
    else:
        v.note('No preview available', 698, 324, 240, 2)
    v.muted('%d players / %d X %d' % (m[2], m[3], m[4]), 676, 460); v.muted(m[5].title(), 676, 486)
    v.add('Back', 38, 550, 192); v.add('Use Map', 758, 550, 220, active=True)
    return paint(v)

def browser(page, theme='red'):
    if page == 'name':
        v = View('settings', 'PLAYER NAME', theme); v.well('YOUR NAME', 38, 110, 600, 350); v.text('Player name', 56, 202)
        v.add('Commander', 56, 238, 564, 40, FIELD, caret=True); v.add('OK', 446, 498, 192, active=True); return paint(v)
    if page == 'wait':
        v = View('settings', 'MULTIPLAYER LOBBY', theme); v.well('ROOM', 38, 110, 600, 350)
        v.note("Commander's Game", 56, 178, 564, 2, col=v.theme.text); v.note('192.168.1.20 : 5555', 56, 246, 564, 2, col=v.theme.accent)
        v.note('Waiting for the host to start the match.', 56, 314, 564, 3)
        v.add('Leave', 38, 498, 192); v.add('Ready', 446, 498, 192, active=True); return paint(v)
    v = View('visuals', 'MULTIPLAYER', theme); host = page == 'host'
    v.add('Host Game', 38, 86, 230, active=host); v.add('Join Game', 276, 86, 230, active=not host)
    if host:
        v.well('GAME', 38, 150, 460, 368); v.well('ACCESS', 518, 150, 460, 368)
        v.text('Game name', 56, 208); v.add("Commander's Game", 56, 232, 424, 40, FIELD, caret=True)
        v.text('Player name', 56, 306); v.add('Commander', 56, 330, 424, 40, FIELD)
        v.add('Set Up Match', 56, 422, 424); v.muted('Isle of Fury (6-8)', 56, 478)
        v.add('Private Game', 536, 204, 424, 36, CHECK, value=0); v.text('Passcode', 536, 270, v.theme.muted); v.add('', 536, 294, 424, 40, FIELD, disabled=True)
        v.add('Internet Game', 536, 356, 424, 36, CHECK, value=0); v.add('List Publicly', 536, 414, 424, 36, CHECK, value=0)
        v.note('Public listing shares your address', 536, 474, 424, 2)
        v.add('Back', 38, 550, 192); v.add('Host', 758, 550, 220, active=True)
    else:
        v.add('LAN', 514, 86, 140, active=True); v.add('Internet', 662, 86, 140)
        v.well('AVAILABLE GAMES', 38, 150, 600, 368); v.well('JOIN DETAILS', 658, 150, 320, 368)
        v.muted('Game', 56, 200); v.muted('Map', 302, 200); v.muted('Players', 534, 200)
        rooms = [("Natasha's Game", 'Path Beyond (6-8)', '3 / 8'), ('Allied training', 'Ridge War (2-4)', '1 / 4'), ('Ivory skirmish', 'Ivory Wastelands (2-4)', '4 / 4')]
        for i, r in enumerate(rooms): v.add(C.fitted(r[0], 220), 56, 240 + i * 48, 564, 44, ROW, active=i == 0)
        v.add('Hide Locked', 56, 408, 424, 36, CHECK, value=0); v.add('Hide Greyed', 56, 454, 424, 36, CHECK, value=1)
        v.text('Player name', 676, 214); v.add('Commander', 676, 238, 284, 40, FIELD)
        v.text('Host address', 676, 312); v.add('192.168.1.20', 676, 336, 284, 40, FIELD, caret=True)
        v.text('Join passcode', 676, 410, v.theme.muted); v.add('', 676, 434, 284, 40, FIELD, disabled=True)
        v.add('Refresh', 38, 550, 192); v.add('Back', 550, 550, 192); v.add('Join', 758, 550, 220, active=True)
        paint(v)
        for i, r in enumerate(rooms):
            C.text(v.img, C.fitted(r[1], 208), 302, 254 + i * 48, v.theme.muted); C.text(v.img, r[2], 534, 254 + i * 48, v.theme.muted)
        return v
    return paint(v)

def ops(grouped, rows, tab=0, sel=0, theme='red', empty_text=None, title=None, tabs=('Counterstrike', 'Aftermath')):
    v = View('visuals', title or ('SPECIAL OPS' if grouped else 'USER MAPS'), theme)
    if grouped:
        for i, t in enumerate(tabs): v.add(t, 38 + i * 238, 86, 230, active=i == tab)
    y = 150 if grouped else 110; h = 368 if grouped else 408
    v.well('MISSIONS', 38, y, 600, h); v.well('DETAILS', 658, y, 320, h)
    for i, r in enumerate(rows[:8]):
        v.add(r[1], 56, y + 48 + i * 34, 564, 32, ROW, active=i == sel, badge=r[2] if grouped else None)
    v.muted('%d missions' % len(rows), 56, 490)
    if len(rows) > 8: v.add('Previous', 346, 474, 132, 32, disabled=True); v.add('Next', 488, 474, 132, 32)
    if not rows and empty_text: v.note(empty_text, 56, y + 64, 564, 6)
    if rows:
        r = rows[sel]
        v.badges.append((r[2], 742, 212, 136, 'plate'))
        v.muted(r[2], 676, 370); v.muted(r[0], 676, 404); v.note(r[1], 676, 438, 284, 2)
    elif empty_text:
        v.note('Nothing to play yet', 676, y + 64, 284, 2)
    v.add('Back', 38, 550, 192); v.add('Play', 758, 550, 220, active=bool(rows), disabled=not rows)
    return paint(v)

def database(st, model=None, theme='red'):
    v = View('lobby', 'DATABASE', theme)
    cats = ['Structures', 'Infantry', 'Vehicles', 'Ships', 'Aircraft']
    for i, c in enumerate(cats): v.add(c, 38 + i * 168, 86, 160, active=i == st['category'])
    v.add('Allied', 890, 86, 132, active=st['house'] == 'Allied', badge='Allied')
    v.add('Soviet', 1046, 86, 132, active=st['house'] == 'Soviet', badge='Soviet')
    v.well('ROSTER', 38, 150, 312, 368); v.well('MODEL PREVIEW', 370, 150, 368, 368); v.well('FIELD DATA', 758, 150, 420, 368)
    for i, n in enumerate(st['names'][:8]): v.add(n, 56, 200 + i * 32, 276, 30, ROW, active=i == st['selected'])
    v.add('Previous', 56, 474, 132, 32, disabled=True); v.add('Next', 200, 474, 132, 32)
    v.muted('%d / %d' % (st['selected'] + 1, len(st['names'])), 194, 458, center=True)
    tabs = ['Info', 'Stats', 'Matchups', 'Tech']; tx = [770, 858, 946, 1086]; tw = [80, 80, 132, 80]
    for i in range(4): v.add(tabs[i], tx[i], 200, tw[i], 40, active=i == st['detail'])
    y = 250
    for line in C.wrapped(st['name'], 384): v.text(line, 776, y); y += 24
    v.accent('%s / $%d' % (st['code'], st['cost']), 776, y + 4); y += 40
    if st['detail'] == 0:
        v.note(st['brief'], 776, y, 384, 5, pitch=24)
        v.muted('Armour', 776, 454); v.text(st['armour'], 952, 454); v.muted('Hit points', 776, 486); v.accent(str(st['hp']), 952, 486)
    elif st['detail'] == 1:
        for i, (a, b) in enumerate(st['stats']): v.muted(a, 776, y + i * 28); v.text(b, 1000, y + i * 28)
    if model is not None:
        m = model.convert('RGBA'); v.img.alpha_composite(m, (388 + (332 - m.width) // 2, 202 + (296 - m.height) // 2))
    v.muted('Arrows: browse / Esc: close', 38, 564); v.add('Close', 958, 550, 220, active=True)
    return paint(v)
