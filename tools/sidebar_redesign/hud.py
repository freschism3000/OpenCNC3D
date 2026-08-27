"""C&C3D sidebar HUD — 640x480. Geometry is loaded from chunks/layout.json,
which slice_ref.py derives by measurement, so code and art can't drift apart."""
from PIL import Image
import os, json

HERE = os.path.dirname(os.path.abspath(__file__))
CH   = os.path.join(HERE, "chunks")
_cfg = json.load(open(os.path.join(CH, "layout.json")))

W, H        = 160, 480
SCREEN_POS  = (480, 0)
RADAR       = tuple(_cfg["RADAR"])                 # x, y, w, h
CELL_W, CELL_H = _cfg["CELL"]
CELL_COLS   = _cfg["COLS"]
CELL_ROWS   = _cfg["ROWS"]
METER_X, METER_Y0, METER_W, _mh = _cfg["METER"]
METER_Y1    = METER_Y0 + _mh
METER_PITCH = _cfg["PITCH"]

def _c(n): return Image.open(os.path.join(CH, f"{n}.png")).convert("RGBA")

def radar_zoom(mw, mh):
    return max(1, min(RADAR[2] // max(1, mw), RADAR[3] // max(1, mh)))

def render(cameos=None, power=0.55, radar_img=None):
    im = _c("chassis").copy()

    if radar_img is not None:
        x, y, w, h = RADAR
        r = radar_img.convert("RGBA")
        pane = Image.new("RGBA", (w, h), (0, 0, 0, 255))
        pane.alpha_composite(r, ((w - r.width) // 2, (h - r.height) // 2))
        im.alpha_composite(pane, (x, y))

    lit = _c("meter_segment_lit")
    n = int(round((METER_Y1 - METER_Y0) / METER_PITCH * max(0.0, min(1.0, power))))
    for k in range(n):
        y = METER_Y1 - (k + 1) * METER_PITCH
        if y < METER_Y0: break
        im.alpha_composite(lit, (METER_X, y))

    if cameos:
        i = 0
        for y in CELL_ROWS:
            for x in CELL_COLS:
                if i < len(cameos) and cameos[i] is not None:
                    c = cameos[i].convert("RGBA")
                    if c.size != (CELL_W, CELL_H):
                        pane = Image.new("RGBA", (CELL_W, CELL_H), (0, 0, 0, 0))
                        pane.alpha_composite(c, ((CELL_W-c.width)//2, (CELL_H-c.height)//2))
                        c = pane
                    im.alpha_composite(c, (x, y))
                i += 1
    return im
