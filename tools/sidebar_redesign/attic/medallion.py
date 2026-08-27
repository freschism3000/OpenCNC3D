"""The GDI medallion, rebuilt as pixel-art relief instead of a blurry upscale.

I do not draw the eagle from imagination; two attempts at that failed. Instead the
SILHOUETTE is lifted from the original painted art (which has a proven shape), scaled
cleanly as a mask, and then re-lit as raised metal with the same faceted renderer that
does the chrome. Shape from the original artist, material from the material system."""
from PIL import Image
import numpy as np, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import kit as K, material as M

def _components(mask):
    h, w = mask.shape
    lab = np.zeros((h, w), int); cur = 0; out = []
    for sy in range(h):
        for sx in range(w):
            if not mask[sy, sx] or lab[sy, sx]: continue
            cur += 1; st = [(sy, sx)]; lab[sy, sx] = cur; n = 0
            while st:
                y, x = st.pop(); n += 1
                for dy, dx in ((1,0),(-1,0),(0,1),(0,-1)):
                    ny, nx = y+dy, x+dx
                    if 0 <= ny < h and 0 <= nx < w and mask[ny, nx] and not lab[ny, nx]:
                        lab[ny, nx] = cur; st.append((ny, nx))
            out.append((n, cur))
    return lab, out

def source_masks(faction="GDI"):
    """Split the painted medallion into (eagle, ring) silhouettes."""
    e = np.array(K.emblem(faction)).astype(int)
    rgb, al = e[:, :, :3], e[:, :, 3] > 0
    warm = (rgb[:,:,0] + rgb[:,:,1]) // 2 - rgb[:,:,2]
    gold = al & (warm >= 25)
    h, w = gold.shape
    yy, xx = np.mgrid[0:h, 0:w]
    cy, cx = h/2, w/2
    rad = np.sqrt(((xx-cx)/(w/2))**2 + ((yy-cy)/(h/2))**2)
    ring  = gold & (rad > 0.78)
    inner = gold & (rad <= 0.78)
    lab, comps = _components(inner)
    if comps:
        comps.sort(reverse=True)
        keep = {c for n, c in comps if n >= comps[0][0] * 0.04}
        inner = np.isin(lab, list(keep))
    return inner, ring, al

def scale_mask(m, w, h):
    im = Image.fromarray((m * 255).astype(np.uint8), "L").resize((w, h), Image.LANCZOS)
    return np.array(im) > 127

def render(size, faction="GDI", rim=M.STEEL, field=M.STEEL, bird=M.BRASS):
    """size: (w,h) of the finished medallion."""
    w, h = size
    eagle_s, ring_s, disc_s = source_masks(faction)
    eagle = scale_mask(eagle_s, w, h)
    disc  = scale_mask(disc_s,  w, h)
    yy, xx = np.mgrid[0:h, 0:w]
    rad = np.sqrt(((xx-(w-1)/2)/(w/2))**2 + ((yy-(h-1)/2)/(h/2))**2)
    outer = rad <= 1.0
    rimband = outer & (rad > 0.80)
    inner = outer & ~rimband

    img = Image.new("RGBA", (w, h), (0,0,0,0))
    # raised rim
    img.alpha_composite(M.facet_shade(outer, bevel=max(2, w//22), ramp=rim,
                                      base=5.0, spec=True))
    # recessed field inside it
    fld = M.recess(w, h, 0, field, bevel=max(2, w//26), floor=6, mask=inner)
    img.alpha_composite(fld)
    # the bird, raised, in brass
    rel = M.facet_shade(eagle, bevel=max(2, w//30), ramp=bird, base=5.4, spec=True)
    sh = Image.new("RGBA", (w, h), (0,0,0,0))
    sp = sh.load()
    for y, x in zip(*np.where(eagle)):
        for dy, dx in ((2,2),(2,1),(1,2)):
            Y, X = y+dy, x+dx
            if 0 <= Y < h and 0 <= X < w and not eagle[Y, X]: sp[X, Y] = bird[0] + (120,)
    img.alpha_composite(sh); img.alpha_composite(rel)
    # hard outline round the whole coin
    px = img.load()
    edge = outer & ~M.inset(outer, 1)
    for y, x in zip(*np.where(edge)): px[x, y] = rim[0] + (255,)
    return img

if __name__ == "__main__":
    for i, s in enumerate([(60,60),(100,100),(134,134)]):
        m = render(s)
        bg = Image.new("RGB", s, (18,20,28)); bg.paste(m, (0,0), m)
        k = 400 // s[0]
        bg.resize((s[0]*k, s[1]*k), Image.NEAREST).save(f"/tmp/med{i}.png")
    print("ok")
