#!/usr/bin/env python3
"""Decode and magnify every texture a cartridge display list binds.

The flat two-triangle models in the model table carry no shape worth rendering:
all the evidence about what they ARE is in the texels. This dumps them 1:1 and
scaled, with the tile format printed, so they can be read by eye.

    python3 g170_textures.py --slot 142 --slot 151 -o /tmp/tex.png
"""
import argparse, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, "support"))
import numpy as np
from PIL import Image
import objgraph2 as O2
import vx_rdp as VX
import render_dl as RD


def textures_of(dl_rom, rom, tlut):
    """Every distinct tile the list binds, in bind order."""
    out, seen = [], set()
    verts, tris = VX.walk(rom, dl_rom, VX.make_resolve(dl_rom))
    for tri in tris:
        st = tri[3] or {}
        timg = st.get("timg")
        if not timg:
            continue
        key = (timg, st.get("fmt"), st.get("siz"), st.get("lrs"), st.get("lrt"))
        if key in seen:
            continue
        seen.add(key)
        off = RD.ram_to_rom(timg)
        w = st["lrs"] // 4 + 1
        h = st["lrt"] // 4 + 1
        img = None
        if off is not None and 0 < w <= 512 and 0 < h <= 512:
            try:
                img = np.array(VX.decode(rom, off, w, h, st["fmt"], st["siz"],
                                         tlut=tlut), dtype=np.uint8).reshape(h, w, 4)
            except Exception as exc:
                img = None
        out.append(dict(ram=timg, rom=off, w=w, h=h, fmt=st.get("fmt"),
                        siz=st.get("siz"), img=img))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--slot", action="append", type=int, default=[])
    ap.add_argument("--dl", action="append", default=[])
    ap.add_argument("--scale", type=int, default=6)
    ap.add_argument("-o", "--out", default="tex.png")
    a = ap.parse_args()
    rom = O2.rom()
    tlut = VX.read_tlut(rom, 0x99130)
    items = []
    for s in a.slot:
        items.append(("slot %d" % s, RD.dl_of_slot(s)))
    for d in a.dl:
        items.append((d, int(d, 16)))
    tiles = []
    for label, dl in items:
        for t in textures_of(dl, rom, tlut):
            print("%-10s dl 0x%07X  tex RAM 0x%08X ROM 0x%06X  %dx%d fmt=%s siz=%s  %s"
                  % (label, dl, t["ram"], t["rom"] or 0, t["w"], t["h"],
                     t["fmt"], t["siz"], "ok" if t["img"] is not None else "UNDECODED"))
            if t["img"] is not None:
                tiles.append((label, t))
    if not tiles:
        return
    S = a.scale
    W = max(t["w"] for _, t in tiles) * S
    H = sum(t["h"] * S + 14 for _, t in tiles)
    sheet = Image.new("RGBA", (W, H), (255, 0, 255, 255))
    y = 0
    for label, t in tiles:
        im = Image.fromarray(t["img"], "RGBA").resize((t["w"] * S, t["h"] * S), Image.NEAREST)
        sheet.paste(im, (0, y))
        y += t["h"] * S + 14
    sheet.save(a.out)
    print("wrote", a.out)


if __name__ == "__main__":
    main()
