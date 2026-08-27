"""Interactive elements as multi-frame SHAPES, which is what the engine supports.

sidebar/dosbar.c does states two ways:
  * db_text_button()  -> a CODE-DRAWN box, style DB_BOX_RAISED / DB_BOX_DOWN /
    DB_BOX_DIS_RAISED, with the caption recoloured WHITE / DKGREY / RED / LTGREY.
  * STRIPUP and STRIPDN -> SHP shapes with 2 FRAMES, drawn via
    db_draw_shape(s, sh, frame, x, y).

An art-driven HUD has to use the second one: a frame index into a sprite. That is a
plain blit, so it ports to Win98/Glide unchanged. A CSS tint has no engine equivalent
and was only ever a test-bed shortcut.

Frames are laid out left to right in one PNG, exactly like the existing pack art
(SHAPES.txt: 'STRIPUP  2  16x12  ->  32x12').

FRAME 0 here is the delivered art. FRAME 1 and 2 are PLACEHOLDERS so the mechanism can
be exercised; they need drawing."""
from PIL import Image
import numpy as np, os, json

HERE = os.path.dirname(os.path.abspath(__file__)); CH = os.path.join(HERE, "chunks")
chassis = Image.open(f"{CH}/chassis.png").convert("RGBA")

# measured plate outlines
ELEMENTS = {
    "button_repair": dict(box=(6,184,56,23),  frames=3, chamfer=5),
    "button_sell":   dict(box=(71,184,34,23), frames=3, chamfer=5),
    "button_map":    dict(box=(115,184,35,23),frames=3, chamfer=5),
    "arrow_up":      dict(box=(18,453,32,25), frames=2, chamfer=7),
    "arrow_down":    dict(box=(53,453,32,25), frames=2, chamfer=7),
}

def chamfer_mask(w, h, r):
    m = np.ones((h, w), bool)
    for (cx, cy) in ((0,0),(w-1,0),(0,h-1),(w-1,h-1)):
        for y in range(h):
            for x in range(w):
                if abs(x-cx)+abs(y-cy) < r: m[y, x] = False
    return m

def variant(base, mask, mul, dy=0, tint=None):
    a = np.array(base.convert("RGBA")).astype(float)
    h, w = mask.shape
    body = np.clip(a[..., :3]*mul, 0, 255)
    if tint is not None:
        body = np.clip(body*0.7 + np.array(tint)*0.3, 0, 255)
    if dy:
        body = np.roll(body, dy, axis=0)
    out = np.zeros((h, w, 4), np.uint8)
    out[..., :3] = body
    out[..., 3] = np.where(mask, 255, 0)
    return Image.fromarray(out, "RGBA")

def build():
    manifest = {}
    for name, spec in ELEMENTS.items():
        x, y, w, h = spec["box"]
        base = chassis.crop((x, y, x+w, y+h))
        m = chamfer_mask(w, h, spec["chamfer"])
        frames = [variant(base, m, 1.0)]                       # 0 normal (real art)
        frames.append(variant(base, m, 0.62, dy=1))            # 1 pressed  (placeholder)
        if spec["frames"] == 3:
            frames.append(variant(base, m, 1.0, tint=(210,60,45)))  # 2 toggled on
        strip = Image.new("RGBA", (w*len(frames), h), (0,0,0,0))
        for i, f in enumerate(frames): strip.alpha_composite(f, (i*w, 0))
        strip.save(f"{CH}/{name}.png")
        manifest[name] = dict(frames=len(frames), w=w, h=h, x=x, y=y)
        print("%-14s %d frames of %dx%d -> strip %dx%d" % (name, len(frames), w, h, strip.width, h))
    json.dump(manifest, open(f"{CH}/frames.json","w"), indent=1)
    return manifest

if __name__ == "__main__":
    build()
