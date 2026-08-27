"""Find where each piece actually sits in the delivered reference, by best fit."""
from PIL import Image
import numpy as np

W, H = 160, 480
ref = np.array(Image.open("incoming/newhud_reference.png").convert("RGB")
               .resize((W, H), Image.LANCZOS)).astype(float)

def load(n):
    a = np.array(Image.open(f"incoming/{n}.png").convert("RGBA")).astype(float)
    return a[..., :3], a[..., 3] > 128

def locate(name, gx, gy, rad=34):
    rgb, m = load(name)
    ph, pw = m.shape
    if m.sum() == 0: return None
    best, bx, by = 1e18, gx, gy
    for dy in range(gy-rad, gy+rad+1):
        if dy < 0 or dy+ph > H: continue
        for dx in range(gx-rad, gx+rad+1):
            if dx < 0 or dx+pw > W: continue
            win = ref[dy:dy+ph, dx:dx+pw]
            err = np.abs(win[m] - rgb[m]).mean()
            if err < best: best, bx, by = err, dx, dy
    return bx, by, round(best, 2)

GUESS = [("panel_header", 0, 0), ("panel_radar_bezel", 0, 19),
         ("button_repair", 4, 184), ("button_sell", 70, 184), ("button_map", 114, 184),
         ("meter_assembled", 0, 210),
         ("arrow_up", 20, 454), ("arrow_down", 52, 454)]
print("%-20s %-14s %-14s %s" % ("piece", "spec guess", "actual", "fit err"))
found = {}
for n, gx, gy in GUESS:
    r = locate(n, gx, gy)
    if r:
        bx, by, e = r
        found[n] = (bx, by)
        print("%-20s (%3d,%3d)      (%3d,%3d)      %.2f%s" % (
            n, gx, gy, bx, by, e, "   <-- moved" if (bx, by) != (gx, gy) else ""))

# cells: search each of the 10 slots around the spec grid
print()
rgb, m = load("cell_well"); ph, pw = m.shape
slots = []
for r, gy in enumerate([210, 258, 306, 354, 402]):
    row = []
    for c, gx in enumerate([18, 88]):
        best, bx, by = 1e18, gx, gy
        for dy in range(gy-30, gy+31):
            if dy < 0 or dy+ph > H: continue
            for dx in range(gx-30, gx+31):
                if dx < 0 or dx+pw > W: continue
                e = np.abs(ref[dy:dy+ph, dx:dx+pw][m] - rgb[m]).mean()
                if e < best: best, bx, by = e, dx, dy
        row.append((bx, by, round(best,2)))
    slots.append(row)
    print("row %d: %s" % (r, row))
xs = sorted({s[0] for row in slots for s in row}); ys = [row[0][1] for row in slots]
print()
print("cell columns x =", xs)
print("cell rows    y =", ys)
if len(ys) > 1: print("row pitch =", [ys[i+1]-ys[i] for i in range(len(ys)-1)])
