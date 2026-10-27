"""Red Alert's own Dialog_Box (redalert/dialog.cpp), drawn exactly as 1996 draws it."""
from PIL import Image
import numpy as np
import chassis as C
def dialog_box(w, h, theme, R=2):
    T = C.THEMES[theme]; im = Image.new('RGBA', (w, h), (0, 0, 0, 0)); cx, cy = w // 2, h // 2
    for i, (dx, dy) in enumerate([(-312, -192), (0, -192), (-312, 0), (0, 0)]):
        t, _ = C.piece('DD-BKGND', i, T); im.paste(t, (cx + dx, cy + dy), t)
    e0, _ = C.piece('DD-EDGE', 0, T); e1, _ = C.piece('DD-EDGE', 1, T)
    for yy in range(0, h, 6): im.paste(e0, (7 * R, yy), e0); im.paste(e1, (w - (7 + 8) * R, yy), e1)
    l, _ = C.piece('DD-LEFT', 0, T); im.paste(l, (0, cy - 100 * R), l); im.paste(l, (0, cy), l)
    r, _ = C.piece('DD-RIGHT', 0, T); im.paste(r, (w - 7 * R, cy - 100 * R), r); im.paste(r, (w - 7 * R, cy), r)
    b, _ = C.piece('DD-BOTM', 0, T); im.paste(b, (cx - 160 * R, h - 8 * R), b); im.paste(b, (cx, h - 8 * R), b)
    t, _ = C.piece('DD-TOP', 0, T); im.paste(t, (cx - 160 * R, 0), t); im.paste(t, (cx, 0), t)
    for i, (x, y) in enumerate([(0, 0), (w - (12 * R - 1), 0), (0, h - 12 * R), (w - (12 * R - 1), h - 12 * R)]):
        c, _ = C.piece('DD-CRNR', i, T); im.paste(c, (x, y), c)
    return im
