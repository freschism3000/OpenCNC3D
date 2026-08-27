"""Ingest externally-generated HUD art.

The source sheets carry several pieces each on a white field. This keys the white to
alpha, finds each island, reports it, and writes it out cropped. Mapping to the spec
sizes happens in a second pass so nothing is squashed before we have looked at it."""
from PIL import Image
import numpy as np, os, sys, json

SRC = "incoming"; OUT = "incoming_cut"
WHITE_TOL = 22          # how close to pure white counts as background
MIN_AREA  = 400         # ignore specks

def key_white(im, tol=WHITE_TOL):
    a = np.array(im.convert("RGBA")).astype(int)
    rgb = a[..., :3]
    near = (rgb.min(axis=2) >= 255 - tol) & (rgb.ptp(axis=2) <= 8)
    # only flood from the border, so white INSIDE a piece survives
    h, w = near.shape
    seen = np.zeros((h, w), bool); bg = np.zeros((h, w), bool)
    from collections import deque
    q = deque([(y, x) for x in range(w) for y in (0, h-1)] +
              [(y, x) for y in range(h) for x in (0, w-1)])
    while q:
        y, x = q.popleft()
        if seen[y, x]: continue
        seen[y, x] = True
        if not near[y, x]: continue
        bg[y, x] = True
        for dy, dx in ((1,0),(-1,0),(0,1),(0,-1)):
            ny, nx = y+dy, x+dx
            if 0 <= ny < h and 0 <= nx < w and not seen[ny, nx]: q.append((ny, nx))
    lum = rgb.mean(axis=2)
    soft = np.clip((255.0 - lum) / max(1.0, tol), 0, 1) * 255.0
    alpha = np.where(bg, 0, np.maximum(soft, 40)).astype(int)
    alpha = np.where(bg, 0, 255)               # default: hard cut
    a[..., 3] = alpha
    return Image.fromarray(a.astype(np.uint8), "RGBA"), ~bg

def islands(mask, min_area=MIN_AREA):
    h, w = mask.shape
    lab = np.zeros((h, w), int); n = 0; boxes = []
    from collections import deque
    for sy in range(h):
        for sx in range(w):
            if not mask[sy, sx] or lab[sy, sx]: continue
            n += 1; q = deque([(sy, sx)]); lab[sy, sx] = n
            x0 = x1 = sx; y0 = y1 = sy; area = 0
            while q:
                y, x = q.popleft(); area += 1
                x0, x1 = min(x0, x), max(x1, x); y0, y1 = min(y0, y), max(y1, y)
                for dy, dx in ((1,0),(-1,0),(0,1),(0,-1),(1,1),(1,-1),(-1,1),(-1,-1)):
                    ny, nx = y+dy, x+dx
                    if 0 <= ny < h and 0 <= nx < w and mask[ny, nx] and not lab[ny, nx]:
                        lab[ny, nx] = n; q.append((ny, nx))
            if area >= min_area: boxes.append((x0, y0, x1, y1, area, n))
    return lab, boxes

def has_alpha(im):
    if im.mode not in ("RGBA", "LA"): return False
    a = np.array(im.convert("RGBA"))[..., 3]
    return bool((a < 250).mean() > 0.02)      # more than 2% non-opaque = real alpha

def run():
    os.makedirs(OUT, exist_ok=True)
    manifest = []
    files = sorted(f for f in os.listdir(SRC)
                   if f.lower().endswith((".png", ".webp", ".jpg", ".jpeg")))
    if not files:
        print("nothing in ./%s" % SRC); return
    for f in files:
        im = Image.open(os.path.join(SRC, f))
        if has_alpha(im):                      # already cut out; do not touch the edges
            keyed = im.convert("RGBA")
            mask = np.array(keyed)[..., 3] > 8
            print("  (already has alpha, skipping white-key)")
        else:
            keyed, mask = key_white(im)
        lab, boxes = islands(mask)
        boxes.sort(key=lambda b: (b[1] // 60, b[0]))          # top-to-bottom, left-to-right
        print("\n%s  %dx%d  -> %d pieces" % (f, im.width, im.height, len(boxes)))
        stem = os.path.splitext(f)[0]
        for i, (x0, y0, x1, y1, area, cid) in enumerate(boxes):
            piece = keyed.crop((x0, y0, x1+1, y1+1))
            sub = (lab[y0:y1+1, x0:x1+1] == cid)
            pa = np.array(piece)
            pa[..., 3] = np.where(sub, pa[..., 3], 0)          # isolate this island only
            piece = Image.fromarray(pa, "RGBA")
            name = "%s_%02d" % (stem, i)
            piece.save(f"{OUT}/{name}.png")
            w, h = piece.size
            print("   %-16s %4dx%-4d  aspect %.2f" % (name, w, h, w/h))
            manifest.append(dict(src=f, name=name, w=w, h=h, aspect=round(w/h, 3),
                                 box=[x0, y0, x1, y1]))
    json.dump(manifest, open(f"{OUT}/manifest.json", "w"), indent=1)
    print("\nwrote %d pieces to ./%s" % (len(manifest), OUT))

if __name__ == "__main__": run()
