"""Read the real DOS fonts out of dossidebar.pack.

Record layout is exactly db_pack_load() in sidebar/dosbar.c:
  name[8], nchars u32, maxw u32, maxh u32,
  width[nchars], ytop[nchars], rows[nchars], cells[nchars*maxw*maxh]
Cell values are 4bpp colour CLASSES, resolved through a 16-entry font palette."""
import struct, os
from PIL import Image

# Repo root, resolved at runtime. CNC3D_ROOT overrides it for an out-of-tree checkout.
_ROOT = os.environ.get("CNC3D_ROOT") or os.path.abspath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))


PACK = os.path.join(_ROOT, "playable", "dossidebar.pack")
HDR = struct.Struct("<8sIIII")

def load(path=PACK):
    blob = open(path, "rb").read()
    magic, ver, nshapes, nfonts, npal = HDR.unpack_from(blob, 0)
    assert magic == b"DOSBAR01", magic
    off = HDR.size
    pal6 = blob[off:off+768]; off += 768
    pal8 = blob[off:off+768]; off += 768
    off += 512                                     # clock table
    for _ in range(nshapes):                       # skip shapes
        off += 12
        frames, w, h = struct.unpack_from("<III", blob, off); off += 12
        off += frames * w * h
    fonts = {}
    for _ in range(nfonts):
        name = blob[off:off+8].rstrip(b"\0").decode(); off += 8
        nchars, maxw, maxh = struct.unpack_from("<III", blob, off); off += 12
        width = blob[off:off+nchars]; off += nchars
        ytop  = blob[off:off+nchars]; off += nchars
        rows  = blob[off:off+nchars]; off += nchars
        n = nchars*maxw*maxh
        cells = blob[off:off+n]; off += n
        fonts[name] = dict(nchars=nchars, maxw=maxw, maxh=maxh,
                           width=width, ytop=ytop, rows=rows, cells=cells)
    return fonts, pal8

def classes(font, ch):
    """The 4bpp class grid for one character, cropped to its advance width."""
    c = ord(ch)
    if c >= font["nchars"]: return None
    mw, mh = font["maxw"], font["maxh"]
    gw = font["width"][c]
    base = c*mw*mh
    return [[font["cells"][base + y*mw + x] for x in range(gw)] for y in range(mh)]

if __name__ == "__main__":
    fonts, pal8 = load()
    for n, f in fonts.items():
        print("%-10s nchars=%3d  maxw=%2d maxh=%2d" % (n, f["nchars"], f["maxw"], f["maxh"]))
    for n, f in fonts.items():
        used = set()
        for c in range(f["nchars"]):
            b = c*f["maxw"]*f["maxh"]
            used |= set(f["cells"][b:b+f["maxw"]*f["maxh"]])
        print("  %-10s classes present: %s" % (n, sorted(used)))
        s = "SIDEBAR"
        print("  widths for %r: %s" % (s, [f["width"][ord(x)] for x in s]))
