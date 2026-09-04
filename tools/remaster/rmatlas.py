"""Build a remastered terrain atlas for one theater, in the pack's own slot order."""
import os, sys, collections
import numpy as np
import xml.etree.ElementTree as ET
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(REPO, 'tools'))
sys.path.insert(0, os.path.join(REPO, 'tools', 'bakery', 'sharecopy', 'assets', 'terrain'))
import meg, dds, n64_terrain as NT, pc_tex_override as P

DATA = os.environ.get('CNC3D_REMASTERED_DATA', '')
if not DATA:
    raise SystemExit('set CNC3D_REMASTERED_DATA to the CnCRemastered/Data directory')
if not DATA.endswith(os.sep):
    DATA += os.sep
THEATER = {'TEMPERAT': ('TD_TERRAIN_TEMPERATE.XML', 'THEATERF_TEMPERATE'),
           'DESERT':   ('TD_TERRAIN_DESERT.XML',   'THEATERF_DESERT'),
           'WINTER':   ('TD_TERRAIN_WINTER.XML',   'THEATERF_WINTER')}

def tileset(root):
    """(template ini-name, icon) -> archive path of frame 0."""
    cfg = meg.Meg(DATA + 'CONFIG.MEG')
    xmlname, flag = THEATER[root]
    t = ET.fromstring(cfg.read('DATA\\XML\\TILESETS\\' + xmlname))
    tc = t.find('TilesetTypeClass')
    rp = tc.findtext('RootTexturePath').strip()
    out = {}
    for tile in tc.find('Tiles'):
        k = tile.find('Key')
        nm = k.findtext('Name').strip().upper()
        sh = int(k.findtext('Shape'))
        fr = [f.text.strip() for f in tile.find('Value').find('Frames') if f.text and f.text.strip()]
        if not fr:
            continue                      # an empty <Frame> is a real blank cell
        full = ('DATA\\ART\\TEXTURES\\SRGB\\' + rp + '\\' + fr[0]).upper()
        out[(nm, sh)] = full.replace('/', '\\').replace('.TGA', '.DDS')
    return out

def build(root, tile=128, gutter=4, page=4096, limit=None):
    th = NT.load_theater(root)
    ts = tileset(root)
    tex = meg.Meg(DATA + 'TEXTURES_TD_SRGB.MEG')
    id2ini = {tid: ini for tid, ini in P._theater_templates(THEATER[root][1])}
    ids = list(range(th['n4'])) + [NT.BANK8_BASE + k for k in range(th['n8'])]
    pitch = tile + 2 * gutter
    cols = page // pitch
    rows = (len(ids) + cols - 1) // cols
    pages = (rows * pitch + page - 1) // page
    rows_per_page = page // pitch
    sheets = [np.zeros((page, page, 4), np.uint8) for _ in range(pages)]
    rect = {}
    missing = []
    for i, tid in enumerate(ids):
        if limit and i >= limit:
            break
        pr = NT.tl_lookup(th, tid)
        if pr is None:
            continue
        ini = id2ini.get(int(pr[0]))
        rel = ts.get((ini.upper(), int(pr[1]))) if ini else None
        blob = tex.read(rel) if rel else None
        if blob is None:
            missing.append((ini, int(pr[1])))
            continue
        px = dds.decode(blob)[..., :3]
        if px.shape[0] != tile:
            raise SystemExit('%s %s is %dx%d, not %d' % (ini, pr[1], px.shape[1], px.shape[0], tile))
        # THE ALPHA IS THE CARTRIDGE'S, upscaled. It decides a mechanism (does our sea
        # show through this cell) and not a colour, so it must not come from the DDS.
        a24 = NT.tile_rgba(th, tid)[..., 3]
        a = np.array(np_resize(a24, tile), np.uint8)
        p, r = divmod(i, cols * rows_per_page)[0], i // cols
        pi = r // rows_per_page
        rr = r % rows_per_page
        cc = i % cols
        x0, y0 = cc * pitch + gutter, rr * pitch + gutter
        s = sheets[pi]
        s[y0:y0+tile, x0:x0+tile, :3] = px
        s[y0:y0+tile, x0:x0+tile, 3] = a
        # replicate the edge outward into the gutter, exactly as the 24px atlas does
        s[y0-gutter:y0, x0:x0+tile] = s[y0:y0+1, x0:x0+tile]
        s[y0+tile:y0+tile+gutter, x0:x0+tile] = s[y0+tile-1:y0+tile, x0:x0+tile]
        s[y0-gutter:y0+tile+gutter, x0-gutter:x0] = s[y0-gutter:y0+tile+gutter, x0:x0+1]
        s[y0-gutter:y0+tile+gutter, x0+tile:x0+tile+gutter] = s[y0-gutter:y0+tile+gutter, x0+tile-1:x0+tile]
        rect[i] = (pi, x0, y0)
    return sheets, rect, missing, dict(tile=tile, gutter=gutter, pitch=pitch, cols=cols,
                                       pages=pages, rows_per_page=rows_per_page, n=len(ids))

def np_resize(a, n):
    from PIL import Image
    return Image.fromarray(a).resize((n, n), Image.BILINEAR)
