"""Slice the flat sidebar reference into renderer chunks.

Crops the white canvas margin off first, then derives every coordinate by measurement
rather than hardcoding, so re-running on a new reference stays correct."""
from PIL import Image
import numpy as np, os, json

SRC, OUT, W, H = "incoming/sidebar_ref.png", "chunks", 160, 480
os.makedirs(OUT, exist_ok=True)

raw = Image.open(SRC).convert("RGB")
a = np.array(raw).astype(int); lum = a.mean(axis=2)
nz = lum < 225                                   # the art sits on a white field
cols = np.where(nz.any(axis=0))[0]; rows = np.where(nz.any(axis=1))[0]
box = (cols.min(), rows.min(), cols.max()+1, rows.max()+1)
print("cropped white margin: %s -> %dx%d" % (box, box[2]-box[0], box[3]-box[1]))
ref = raw.crop(box).resize((W, H), Image.LANCZOS).convert("RGBA")

A = np.array(ref).astype(int); L = A[:, :, :3].mean(axis=2)

def longest_run(v, thr):
    best = (0, 0, 0); s = None
    for i, b in enumerate(v < thr):
        if b and s is None: s = i
        elif not b and s is not None:
            if i-s > best[0]: best = (i-s, s, i-1)
            s = None
    if s is not None and len(v)-s > best[0]: best = (len(v)-s, s, len(v)-1)
    return best

# radar: the big solid-black interior in the top third
st, en = [], []
for y in range(25, 180):
    n, s, e = longest_run(L[y], 12)
    if n > 90: st.append(s); en.append(e)
rx0, rx1 = int(np.median(st)), int(np.median(en))
tp, bt = [], []
for x in range(rx0+6, rx1-6, 4):
    n, s, e = longest_run(L[:, x], 12)
    if n > 80: tp.append(s); bt.append(e)
ry0, ry1 = int(np.median(tp)), int(np.median(bt))
RADAR = (rx0, ry0, rx1-rx0+1, ry1-ry0+1)

# meter: the green column, and the channel rail beside it
g = (A[:,:,1] > 70) & (A[:,:,1] > A[:,:,0]+28) & (A[:,:,1] > A[:,:,2]+28)
gx = np.where(g.any(axis=0))[0]
rail = gx.max() + 3
run = [];  s = None
for i, b in enumerate(L[:, rail] > 62):
    if b and s is None: s = i
    elif not b and s is not None: run.append((s, i-1)); s = None
if s is not None: run.append((s, len(L)-1))
run = [r for r in run if r[1]-r[0] > 15]
MY0, MY1 = run[0][0], run[-1][1]
MX, MW = gx.min()-3, (gx.max()+3) - (gx.min()-3) + 1

# cells: profile the build area instead of region-growing, which drifted
def dark_runs(v, thr, minlen):
    out=[]; st=None
    for i,bb in enumerate(v < thr):
        if bb and st is None: st=i
        elif not bb and st is not None:
            if i-st >= minlen: out.append((st, i-1))
            st=None
    if st is not None and len(v)-st >= minlen: out.append((st, len(v)-1))
    return out

rowspans = [r for r in dark_runs(L[:, 40:52].mean(axis=1), 44, 30) if r[0] >= MY0 - 4]
ROWS = [int(r[0]) for r in rowspans][:5]
CH   = int(np.median([r[1]-r[0] for r in rowspans[:5]]))
band = L[ROWS[0]+4:ROWS[0]+CH-4].mean(axis=0)
colspans = dark_runs(band, 42, 30)[:2]
COLS = [int(c[0]) for c in colspans]
CW   = int(np.median([c[1]-c[0]+1 for c in colspans]))

# segment pitch from the green runs
gr = np.where(g.any(axis=1))[0]
runs=[];s=gr[0]
for i in range(1,len(gr)):
    if gr[i]!=gr[i-1]+1: runs.append((s,gr[i-1])); s=gr[i]
runs.append((s,gr[-1]))
PITCH = int(round(np.median([runs[i+1][0]-runs[i][0] for i in range(len(runs)-1)]))) if len(runs)>2 else 12
LIT = runs[-1][0]; UNLIT = max(MY0+4, runs[0][0]-2*PITCH)

seg_lit   = ref.crop((MX, LIT,   MX+MW, LIT+PITCH))
seg_unlit = ref.crop((MX, UNLIT, MX+MW, UNLIT+PITCH))
seg_lit.save(f"{OUT}/meter_segment_lit.png"); seg_unlit.save(f"{OUT}/meter_segment_unlit.png")

chassis = ref.copy()
y = MY0
while y < MY1:
    hh = min(PITCH, MY1-y)
    chassis.paste(seg_unlit.crop((0,0,MW,hh)), (MX, y)); y += PITCH
chassis.save(f"{OUT}/chassis.png")
ref.crop((COLS[0], ROWS[0], COLS[0]+CW, ROWS[0]+CH)).save(f"{OUT}/cell_well.png")

I=lambda v:[int(x) for x in v] if isinstance(v,(list,tuple)) else int(v)
cfg = dict(RADAR=I(RADAR), CELL=I([CW,CH]), COLS=I(COLS), ROWS=I(ROWS),
           METER=I([MX,MY0,MW,MY1-MY0]), PITCH=int(PITCH))
json.dump(cfg, open(f"{OUT}/layout.json","w"), indent=1)
print(json.dumps(cfg, indent=1))
print("edge check: chassis col0 lum %.1f  col159 lum %.1f (should be dark, not ~255)"
      % (np.array(chassis.convert('RGB'))[:,0].mean(), np.array(chassis.convert('RGB'))[:,159].mean()))
