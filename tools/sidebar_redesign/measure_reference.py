"""Measure an assembled HUD reference and emit the slot table at 160x480.

Finds the recessed (dark) openings, classifies them by shape and arrangement, and
reports coordinates in sidebar-local space. Also writes an overlay so the detection can
be eyeballed rather than trusted."""
from PIL import Image, ImageDraw
import numpy as np, sys, os
from collections import deque

W, H = 160, 480
SRC = sys.argv[1] if len(sys.argv) > 1 else "incoming/hud_reference.png"

def regions(dark, min_area):
    h, w = dark.shape
    lab = np.zeros((h, w), int); n = 0; out = []
    for sy in range(h):
        for sx in range(w):
            if not dark[sy, sx] or lab[sy, sx]: continue
            n += 1; q = deque([(sy, sx)]); lab[sy, sx] = n
            x0 = x1 = sx; y0 = y1 = sy; area = 0
            while q:
                y, x = q.popleft(); area += 1
                x0, x1 = min(x0, x), max(x1, x); y0, y1 = min(y0, y), max(y1, y)
                for dy, dx in ((1,0),(-1,0),(0,1),(0,-1)):
                    ny, nx = y+dy, x+dx
                    if 0 <= ny < h and 0 <= nx < w and dark[ny, nx] and not lab[ny, nx]:
                        lab[ny, nx] = n; q.append((ny, nx))
            fill = area / float((x1-x0+1)*(y1-y0+1))
            if area >= min_area and fill > 0.55:
                out.append(dict(x=x0, y=y0, w=x1-x0+1, h=y1-y0+1, area=area, fill=round(fill,2)))
    return out

def main():
    if not os.path.exists(SRC):
        print("no file at", SRC); return
    src = Image.open(SRC).convert("RGBA")
    im = src.resize((W, H), Image.LANCZOS)
    a = np.array(im).astype(int)
    op = a[..., 3] > 100
    lum = a[..., :3].mean(axis=2)
    print("source %dx%d  aspect %.4f  (target %.4f)" % (src.width, src.height,
          src.width/src.height, W/H))

    best = None
    for thr in range(20, 70, 4):                 # sweep: pick the cut that yields 10 cells
        r = regions(op & (lum < thr), 120)
        cells = [q for q in r if 30 <= q["w"] <= 80 and 25 <= q["h"] <= 80]
        if len(cells) == 10 and (best is None or len(r) < best[1]):
            best = (thr, len(r), r)
    thr, _, r = best if best else (40, 0, regions(op & (lum < 40), 120))
    print("threshold %d -> %d regions\n" % (thr, len(r)))

    cells = sorted([q for q in r if 30 <= q["w"] <= 80 and 25 <= q["h"] <= 80],
                   key=lambda q: (q["y"], q["x"]))
    others = [q for q in r if q not in cells]
    radar = max(others, key=lambda q: q["area"]) if others else None
    meter = next((q for q in others if q["h"] > 3*q["w"]), None)

    if radar: print("RADAR   x=%-3d y=%-3d %3dx%-3d" % (radar["x"], radar["y"], radar["w"], radar["h"]))
    if meter: print("METER   x=%-3d y=%-3d %3dx%-3d" % (meter["x"], meter["y"], meter["w"], meter["h"]))
    if cells:
        cols = sorted({q["x"] for q in cells}); rows = sorted({q["y"] for q in cells})
        print("CELLS   %d found, %dx%d each" % (len(cells), cells[0]["w"], cells[0]["h"]))
        print("        columns x = %s" % cols)
        print("        rows    y = %s" % rows)
        print("        aspect %.3f" % (cells[0]["w"]/cells[0]["h"]))
    for q in others:
        if q is radar or q is meter: continue
        print("other   x=%-3d y=%-3d %3dx%-3d fill=%.2f" % (q["x"], q["y"], q["w"], q["h"], q["fill"]))

    ov = im.convert("RGB").resize((W*3, H*3), Image.NEAREST)
    d = ImageDraw.Draw(ov)
    for q, col in [(radar,(255,80,80)), (meter,(80,160,255))] + [(c,(90,255,120)) for c in cells]:
        if not q: continue
        d.rectangle([q["x"]*3, q["y"]*3, (q["x"]+q["w"])*3-1, (q["y"]+q["h"])*3-1], outline=col)
    ov.save("reference_slots.png")
    print("\noverlay -> reference_slots.png")

if __name__ == "__main__": main()
