#!/usr/bin/env python3
"""Build Red Alert's two Enhanced menu emblems as low poly models, from the approved concepts.

  Allied: the 1996 plaque (concept allied/c1, no globe): a gold triangular frame, a silver
          field, and on it the gold eagle in chevrons cut by the silver Y, its small triangle
          and its stem.
  Soviet: the logo movie's star (concept soviet/c1): a hammered silver rim, a red field, the
          gold sunburst, and the hammer and sickle in the radar logo's bronze gold.

Every outline is set by hand in the concept's front view pixels (the front quarter of
concept.png, 627 pixels square), so the model follows the drawing; the 1996 references the
concepts were drawn from sit beside them. Blender (headless, blender_extrude.py) extrudes and
chamfers each part the same depth front and back, so the emblem reads the same from both faces
as the menu turns it. Each material is one 32 x 32 nearest-sampled texture laid flat across the
emblem, the way the Tiberian Dawn menu emblems are textured.

Writes menu_ra_emblem_meshes.h (compiled into menu/enhanced_menu.cpp), mesh.json, the
textures (tex-*.png), and renders made the way the menu draws them (render-*.png and the review
sheet, emblems-review.png). Needs Blender 4 on the PATH (BLENDER names another)."""
import json, math, os, shutil, subprocess, sys, tempfile
import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
BLENDER = os.environ.get('BLENDER') or shutil.which('blender') or '/Applications/Blender.app/Contents/MacOS/Blender'

# ---- outline helpers (concept pixels, y down) ------------------------------------------
def inset(poly, d):
    """The polygon with every edge moved d pixels inward, corners mitred (counter-clockwise
    on screen means inward is to the left of each edge in y-down coordinates)."""
    P = np.array(poly, float); n = len(P)
    area = sum(P[i, 0] * P[(i + 1) % n, 1] - P[(i + 1) % n, 0] * P[i, 1] for i in range(n))
    sgn = 1 if area > 0 else -1
    lines = []
    for i in range(n):
        a, b = P[i], P[(i + 1) % n]; t = (b - a) / np.linalg.norm(b - a)
        nrm = np.array([-t[1], t[0]]) * sgn          # inward normal
        lines.append((a + nrm * d, t))
    out = []
    for i in range(n):
        (p1, t1), (p2, t2) = lines[i - 1], lines[i]
        A = np.array([t1, -t2]).T; s = np.linalg.solve(A, p2 - p1)
        out.append(tuple(p1 + t1 * s[0]))
    return out

def arc(c, r, a0, a1, steps):
    return [(c[0] + r * math.cos(math.radians(a0 + (a1 - a0) * k / steps)),
             c[1] + r * math.sin(math.radians(a0 + (a1 - a0) * k / steps))) for k in range(steps + 1)]

def bar(p0, p1, w):
    (x0, y0), (x1, y1) = p0, p1; dx, dy = x1 - x0, y1 - y0; L = math.hypot(dx, dy)
    nx, ny = -dy / L * w / 2, dx / L * w / 2
    return [(x0 + nx, y0 + ny), (x1 + nx, y1 + ny), (x1 - nx, y1 - ny), (x0 - nx, y0 - ny)]

def mirror(pts, cx): return [(2 * cx - x, y) for x, y in pts]

# ---- the Allied plaque ----------------------------------------------------------------
def allied():
    cx = 318.0
    outer = [(34, 70), (602, 70), (cx, 562)]
    tri = lambda d: [(34 + d * math.sqrt(3), 70 + d), (602 - d * math.sqrt(3), 70 + d), (cx, 562 - 2 * d)]
    frame_in = tri(30); field = tri(24)
    # the eagle: wing tips, wings sloping to the neck, the head facing right with its beak
    head = [(275, 172), (280, 131), (292, 119), (322, 115), (348, 121), (368, 136), (373, 151),
            (365, 149), (357, 146), (353, 158), (361, 172)]
    wing_l = [(125, 131), (155, 113)]
    wing_r = [(481, 113), (511, 131)]
    motif = wing_l + head + wing_r + [(cx, 466)]
    # the silver cut: the small triangle, its stem, and the Y's two arms meeting the stem
    g, sw = 4.5, 4.0
    yc = lambda x0, x1, y0, y1: (lambda y: x0 + (x1 - x0) * (y - y0) / (y1 - y0))
    armR = yc(444, 320, 188, 372)
    def y_at(f, x, off):            # where the arm's edge (f + off) reaches x
        lo, hi = 188.0, 372.0
        for _ in range(60):
            m = (lo + hi) / 2
            if f(m) + off > x: lo = m
            else: hi = m
        return (lo + hi) / 2
    tl, tr, ap = (cx - 60, 205), (cx + 60, 205), (cx, 290)
    yt = 205 + (60 - sw) / 60 * 85
    yu = y_at(armR, cx + sw, -g); yl = y_at(armR, cx + sw, g)
    right = [(cx + sw, yt), (cx + sw, yu), (armR(188) - g, 188), (armR(188) + g, 188), (cx + sw, yl), (cx + sw, 455)]
    left = [(2 * cx - x, y) for x, y in reversed(right)]
    cut = [tl, tr] + right + left
    return {'size': (627, 627), 'centre': (cx, 316.0), 'half': 284.0,
            'parts': [
                {'name': 'frame', 'material': 'gold', 'loops': [outer, frame_in], 'extrude': 0.055, 'bevel': 0.022},
                {'name': 'field', 'material': 'silver', 'loops': [field], 'extrude': 0.040},
                {'name': 'eagle', 'material': 'gold', 'loops': [motif, cut], 'extrude': 0.052, 'bevel': 0.004},
            ],
            'materials': ['gold', 'silver']}

# ---- the Soviet star ------------------------------------------------------------------
def soviet():
    cx = 313.0
    half = [(cx, 22), (404, 151), (591, 204), (453, 340), (529, 561), (cx, 497)]
    star = [half[0], half[1], half[2], half[3], half[4], half[5]] + mirror([half[4], half[3], half[2], half[1]], cx)
    rim_in = inset(star, 44); field = inset(star, 38)
    # the sunburst behind the hammer and sickle: ten rays, one straight down, and the burst
    # at their root; each ray ends on the field's edge
    C = np.array([262.0, 318.0]); F = np.array(field)
    def hit(theta):
        d = np.array([math.cos(theta), math.sin(theta)]); best = 1e9
        for i in range(len(F)):
            a, b = F[i], F[(i + 1) % len(F)]; e = b - a
            M = np.array([d, -e]).T
            if abs(np.linalg.det(M)) < 1e-9: continue
            t, u = np.linalg.solve(M, a - C)
            if t > 0 and 0 <= u <= 1: best = min(best, t)
        return tuple(C + d * (best - 2))
    rays = []
    for k in range(10):
        a = math.radians(90 + 36 * k); w = math.radians(6.5)
        rays.append([tuple(C), hit(a - w), hit(a + w)])
    burst = [tuple(C + np.array([math.cos(math.radians(22.5 * k)), math.sin(math.radians(22.5 * k))]) * (52 if k % 2 == 0 else 30)) for k in range(16)]
    # the sickle: a crescent from its point at the top round to the handle, and the handle
    O, R, t0, t1 = (325.0, 284.0), 112.0, -116.0, 140.0
    ts = [t0 + (t1 - t0) * k / 18 for k in range(19)]
    width = lambda t: 31.0 * min(1.0, (t - t0) / 110.0) ** 0.7     # pointed at the tip
    outer_arc = [(O[0] + R * math.cos(math.radians(t)), O[1] + R * math.sin(math.radians(t))) for t in ts]
    inner_arc = [(O[0] + (R - width(t)) * math.cos(math.radians(t)), O[1] + (R - width(t)) * math.sin(math.radians(t))) for t in reversed(ts[1:])]
    sickle = outer_arc + inner_arc
    s_handle = bar((255, 350), (170, 426), 26)
    # the hammer: a handle at 43 degrees from the head down to the lower right, and its head
    h0, h1 = np.array([232.0, 248.0]), np.array([446.0, 450.0])
    d = (h1 - h0) / np.linalg.norm(h1 - h0); n = np.array([-d[1], d[0]])
    handle = bar(tuple(h0 + d * 10), tuple(h1), 26)
    head = [tuple(h0 + n * 56 - d * 22), tuple(h0 + n * 56 + d * 20), tuple(h0 - n * 50 + d * 20), tuple(h0 - n * 50 - d * 22)]
    parts = [{'name': 'rim', 'material': 'silver', 'loops': [star, rim_in], 'extrude': 0.058, 'bevel': 0.024},
             {'name': 'field', 'material': 'red', 'loops': [field], 'extrude': 0.038},
             {'name': 'burst', 'material': 'yellow', 'loops': [burst], 'extrude': 0.0405}]
    parts += [{'name': 'ray%d' % k, 'material': 'yellow', 'loops': [r], 'extrude': 0.0402} for k, r in enumerate(rays)]
    parts += [{'name': 'sickle', 'material': 'bronze', 'loops': [sickle], 'extrude': 0.058, 'bevel': 0.008},
              {'name': 'sickle-handle', 'material': 'bronze', 'loops': [s_handle], 'extrude': 0.058, 'bevel': 0.008},
              {'name': 'hammer', 'material': 'bronze', 'loops': [handle], 'extrude': 0.066, 'bevel': 0.008},
              {'name': 'hammer-head', 'material': 'bronze', 'loops': [head], 'extrude': 0.066, 'bevel': 0.008}]
    return {'size': (627, 627), 'centre': (cx, 300.0), 'half': 280.0, 'parts': parts,
            'materials': ['silver', 'red', 'yellow', 'bronze']}

# ---- textures: tileless 32 x 32 metal and cloth, colours from the concepts --------------
def noise(seed, octaves=((4, 1.0), (8, .5), (16, .25))):
    rng = np.random.default_rng(seed); out = np.zeros((32, 32))
    for cells, amp in octaves:
        g = rng.random((cells + 1, cells + 1))
        img = Image.fromarray((g * 255).astype(np.uint8)).resize((32, 32), Image.BICUBIC)
        out += (np.array(img, float) / 255 - .5) * amp
    return (out - out.min()) / (out.max() - out.min() + 1e-9)

def ramp(t, stops):
    t = np.clip(t, 0, 1); xs = [s[0] for s in stops]
    return np.stack([np.interp(t, xs, [s[1][c] for s in stops]) for c in range(3)], -1)

# The front of an emblem is lit about 0.79 by the menu's light, so these are the concepts'
# colours over that.
TEXTURES = {
    'gold':   (11, [(0, (96, 60, 14)), (.35, (192, 136, 40)), (.7, (246, 196, 76)), (1, (255, 240, 160))]),
    'silver': (23, [(0, (70, 74, 78)), (.35, (138, 142, 146)), (.7, (204, 206, 208)), (1, (250, 250, 248))]),
    'red':    (31, [(0, (150, 8, 6)), (.5, (205, 18, 12)), (1, (235, 40, 24))]),
    'yellow': (37, [(0, (240, 170, 10)), (.6, (255, 205, 20)), (1, (255, 230, 70))]),
    'bronze': (41, [(0, (170, 96, 28)), (.45, (226, 148, 56)), (.8, (250, 186, 92)), (1, (255, 214, 140))]),
}
def texture(name):
    seed, stops = TEXTURES[name]
    n = noise(seed)
    if name == 'silver':                    # hammered: a finer second layer of dents
        n = .6 * n + .4 * noise(seed + 1, ((16, 1.0), (32, .6)))
    rgb = np.clip(ramp(n, stops), 0, 255).astype(np.uint8)
    return np.dstack([rgb, np.full((32, 32), 255, np.uint8)])

# ---- build ----------------------------------------------------------------------------
def build(name, E, work):
    cx, cy = E['centre']; s = 1.0 / E['half']
    spec = {'parts': []}
    for p in E['parts']:
        q = dict(p); q['loops'] = [[((x - cx) * s, -(y - cy) * s) for x, y in loop] for loop in p['loops']]
        spec['parts'].append(q)
    # Blender's chamfer grows a part by its bevel on every edge; outlines are set back and
    # holes opened by the same amount so the drawn edges land where the concept has them
    for q, p in zip(spec['parts'], E['parts']):
        b = p.get('bevel', 0.0) / s
        if b > 0:
            q['loops'] = [[((x - cx) * s, -(y - cy) * s) for x, y in inset(loop, b if i == 0 else -b)] for i, loop in enumerate(p['loops'])]
    sp, op = os.path.join(work, name + '-in.json'), os.path.join(work, name + '-out.json')
    json.dump(spec, open(sp, 'w'))
    subprocess.run([BLENDER, '-b', '--factory-startup', '--python', os.path.join(HERE, 'blender_extrude.py'), '--', sp, op],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    parts = json.load(open(op))
    mats = E['materials']; verts = []; mat_of = []
    allv = np.array([v for p in parts for t in p['tris'] for v in t['v']])
    lo, hi = allv[:, :2].min(0), allv[:, :2].max(0); mid = (lo + hi) / 2; k = 1.0 / max(hi - lo) * 2
    for p in parts:
        m = mats.index(p['material'])
        for t in p['tris']:
            n = np.array(t['n']); n = n / (np.linalg.norm(n) + 1e-12)
            for v in t['v']:
                x, y, z = (v[0] - mid[0]) * k, (v[1] - mid[1]) * k, v[2] * k
                verts += [x, y, z, n[0], n[1], n[2], (x + 1) / 2, (y + 1) / 2]
            mat_of.append(m)
    texs = [texture(m) for m in mats]
    return np.array(verts, np.float32).reshape(-1, 24), np.array(mat_of, np.uint8), texs, mats

# ---- the menu's own way of drawing them (enhanced_menu.cpp faction_badges) --------------
def render(mesh, mats, texs, angle, size=400, bg=(0, 0, 0)):
    img = np.zeros((size, size, 3), np.float32); img[:] = np.array(bg) / 255; zb = np.full((size, size), -1e9)
    c, s, a, b = math.cos(angle), math.sin(angle), math.cos(.34), math.sin(.34)
    def rot(p):
        u, w = c * p[0] + s * p[2], -s * p[0] + c * p[2]
        return np.array([u, a * p[1] - b * w, b * p[1] + a * w])
    for t, m in zip(mesh, mats):
        V = t.reshape(3, 8); P = np.array([rot(v[:3]) for v in V]); N = np.array([rot(v[3:6]) for v in V])
        X = size / 2 + P[:, 0] * size * .43; Y = size / 2 - P[:, 1] * size * .43; Z = P[:, 2]
        light = np.array([.36 + .7 * max(0, n[0] * .25 + n[1] * .5 + n[2] * .83) + .42 * max(0, n[0] * .13 + n[1] * .25 + n[2] * .96) ** 30 for n in N])
        x0, x1 = int(max(0, math.floor(X.min()))), int(min(size - 1, math.ceil(X.max())))
        y0, y1 = int(max(0, math.floor(Y.min()))), int(min(size - 1, math.ceil(Y.max())))
        if x0 > x1 or y0 > y1: continue
        den = (Y[1] - Y[2]) * (X[0] - X[2]) + (X[2] - X[1]) * (Y[0] - Y[2])
        if abs(den) < 1e-12: continue
        yy, xx = np.mgrid[y0:y1 + 1, x0:x1 + 1] + .5
        l0 = ((Y[1] - Y[2]) * (xx - X[2]) + (X[2] - X[1]) * (yy - Y[2])) / den
        l1 = ((Y[2] - Y[0]) * (xx - X[2]) + (X[0] - X[2]) * (yy - Y[2])) / den; l2 = 1 - l0 - l1
        inside = (l0 >= 0) & (l1 >= 0) & (l2 >= 0)
        z = l0 * Z[0] + l1 * Z[1] + l2 * Z[2]
        reg = zb[y0:y1 + 1, x0:x1 + 1]; draw = inside & (z >= reg)
        if not draw.any(): continue
        u = l0 * V[0, 6] + l1 * V[1, 6] + l2 * V[2, 6]; v = 1 - (l0 * V[0, 7] + l1 * V[1, 7] + l2 * V[2, 7])
        T = texs[m]; tu = np.clip((u * 32).astype(int), 0, 31); tv = np.clip((v * 32).astype(int), 0, 31)
        col = T[tv, tu, :3].astype(np.float32) / 255 * (l0 * light[0] + l1 * light[1] + l2 * light[2])[..., None]
        img[y0:y1 + 1, x0:x1 + 1][draw] = np.clip(col[draw], 0, 1); reg[draw] = z[draw]
    return Image.fromarray((img * 255).astype(np.uint8))

def header(models):
    out = ['/* Red Alert\'s Enhanced menu emblems, low poly, built by build_emblems.py from the approved',
           '   concepts. Each triangle: three corners of position, normal and UV (24 floats); one',
           '   material per triangle; each material one 32 x 32 RGBA texture. Generated: do not edit. */']
    for name, (mesh, mats, texs, mnames) in models.items():
        fl = ','.join(('%.6g' % v) + ('' if '.' in ('%.6g' % v) or 'e' in ('%.6g' % v) else '.') + 'f' for v in mesh.reshape(-1))
        out.append('static const float menu_ra_%s[]={%s};' % (name, fl))
        out.append('static const unsigned char menu_ra_%s_materials[]={%s};' % (name, ','.join(map(str, mats))))
        pix = np.zeros((4, 32, 32, 4), np.uint8)
        for i, t in enumerate(texs): pix[i] = t
        out.append('static const unsigned char menu_ra_%s_textures[]={%s};' % (name, ','.join(map(str, pix.reshape(-1)))))
        out.append('static const int menu_ra_%s_texture_count=%d; /* %s */' % (name, len(texs), ', '.join(mnames)))
    return '\n'.join(out) + '\n'

def sheet(models):
    """Each model beside its concept and its 1996 reference: turning as the menu turns it (the
    still is the angle hidden captures use), and at the sizes the menu draws it."""
    W, H = 2560, 1780; out = Image.new('RGB', (W, H), (24, 24, 26)); d = ImageDraw.Draw(out)
    d.text((20, 12), 'Red Alert Enhanced menu emblems: the low poly models (right) beside the approved concepts and the 1996 references', fill=(230, 222, 206))
    for r, (name, (mesh, mats, texs, mn)) in enumerate(models.items()):
        y = 40 + r * 870
        ref = Image.open(os.path.join(HERE, name, 'c1', '1_ref_1996_radar_logo.png')).convert('RGB').resize((400, 353))
        con = Image.open(os.path.join(HERE, name, 'c1', 'concept.png')).convert('RGB'); con = con.crop((0, 0, con.width // 2, con.height // 2)).resize((400, 400), Image.LANCZOS)
        out.paste(ref, (20, y + 30)); out.paste(con, (440, y + 30))
        d.text((20, y + 10), '1996 radar logo', fill=(196, 160, 150)); d.text((440, y + 10), 'approved concept (front)', fill=(196, 160, 150))
        for k, (ang, lab) in enumerate(((.25, 'model, still angle'), (1.0, 'turning'), (2.2, 'turning'), (math.pi + .25, 'the back'))):
            out.paste(render(mesh, mats, texs, ang), (880 + k * 410, y + 30)); d.text((880 + k * 410, y + 10), lab, fill=(231, 167, 0))
        d.text((880, y + 440), 'at their 1440p pixel sizes: main menu corner, details plate, row badge', fill=(196, 160, 150))
        x = 880
        for sz in (400, 212, 56):
            out.paste(render(mesh, mats, texs, .25, size=sz), (x, y + 460)); x += sz + 30
        d.text((20, y + 400), '%s: %d triangles, %s, 32 x 32 nearest-sampled textures' % (name.title(), len(mesh), ', '.join(mn)), fill=(230, 222, 206))
    out.save(os.path.join(HERE, 'emblems-review.png'), optimize=True)

def main():
    work = tempfile.mkdtemp(prefix='ra-emblems-')
    models = {'allied': build('allied', allied(), work), 'soviet': build('soviet', soviet(), work)}
    open(os.path.join(HERE, 'menu_ra_emblem_meshes.h'), 'w').write(header(models))
    js = {}
    for name, (mesh, mats, texs, mnames) in models.items():
        js[name] = {'triangles': int(len(mesh)), 'materials': mnames}
        for t, m in zip(texs, mnames): Image.fromarray(t, 'RGBA').save(os.path.join(HERE, 'tex-%s-%s.png' % (name, m)))
    json.dump(js, open(os.path.join(HERE, 'mesh.json'), 'w'), indent=1)
    sheet(models)
    shutil.rmtree(work, ignore_errors=True)
    print(json.dumps(js))

if __name__ == '__main__':
    main()
