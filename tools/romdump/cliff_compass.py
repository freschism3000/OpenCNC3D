#!/usr/bin/env python3
"""
CNC3D -- the cliff compass, checked PER TEMPLATE PLACEMENT.

docs/terrain-elevation-grammar.md section 3 averages the height gradient over every
CELL of every SLOPE template and reports one high side per template. The map editor's
AUTO HEIGHTMAP works on whole PLACEMENTS instead -- one stamped template, w x h cells --
so this script asks the question the editor's arithmetic actually asks: over the 55
cartridge maps that ship a heightmap, how often does a placement's own measured gradient
agree with the compass entry the editor would look up for it?

    python3 tools/romdump/cliff_compass.py

Reads the extracted tree (tools/bakery/sharecopy/assets/extracted) and
game/edit_tables.h. Read-only.
"""
import collections, math, os, re, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
EX  = os.path.join(HERE, "..", "bakery", "sharecopy", "assets", "extracted")
TBL = os.path.join(HERE, "..", "..", "game", "edit_tables.h")

# The compass, exactly as the editor's table states it: one of eight directions per
# SLOPE template, as (dx, dy) with dy POSITIVE SOUTH -- the grid's own axes.
DIR = {"N": (0, -1), "E": (1, 0), "S": (0, 1), "W": (-1, 0),
       "NE": (1, -1), "SE": (1, 1), "SW": (-1, 1), "NW": (-1, -1)}
HIGH = {}
for i in range(1, 8):   HIGH[i]  = "N"
for i in range(8, 15):  HIGH[i]  = "E"
for i in range(15, 22): HIGH[i]  = "S"
for i in range(22, 29): HIGH[i]  = "W"
for i in (29, 35, 38):  HIGH[i]  = "NE"
for i in (30, 36, 37):  HIGH[i]  = "SE"
for i in (31, 33):      HIGH[i]  = "SW"
for i in (32, 34):      HIGH[i]  = "NW"

LEVEL = 3.0     # bytes per cell; below this a placement is called level, not backwards


def template_wh():
    """w,h per template id, read out of the editor's own table."""
    wh, tid = {}, 0
    on = False
    for line in open(TBL, errors="ignore"):
        if "EDIT_TEMPLATES[EDIT_TEMPLATE_COUNT]" in line:
            on = True; continue
        if not on:
            continue
        m = re.match(r'\s*\{\s*"([^"]+)",.*?\}\s*,\s*(\d+)\s*,\s*(\d+)\s*\}', line)
        if m:
            wh[tid] = (m.group(1), int(m.group(2)), int(m.group(3)))
            tid += 1
        if line.strip().startswith("};"):
            break
    return wh


WH = template_wh()

_th = {}
def theater(root):
    if root not in _th:
        rd = lambda p: open(os.path.join(EX, p), "rb").read()
        _th[root] = dict(n4=os.path.getsize(os.path.join(EX, f"DA4/{root}.DA4")) // 288,
                         n8=os.path.getsize(os.path.join(EX, f"DA8/{root}.DA8")) // 576,
                         tl4=rd(f"TL4/{root}.TL4"), tl8=rd(f"TL8/{root}.TL8"))
    return _th[root]


def ini(scen):
    d, sec = {}, None
    for line in open(os.path.join(EX, "INI", scen + ".INI"), errors="ignore"):
        s = line.strip()
        if s.startswith("["):
            sec = s.strip("[]").upper()
        elif "=" in s and sec:
            k, v = s.split("=", 1)
            d[sec + "." + k.strip().upper()] = v.strip()
    return d


class Scen:
    def __init__(self, s):
        self.id = s
        d = ini(s)
        th = d.get("MAP.THEATER", "TEMPERATE").upper()
        root = {"DESERT": "DESERT", "TEMPERATE": "TEMPERAT"}.get(th, th[:8])
        self.X = int(d.get("MAP.X", 0)); self.Y = int(d.get("MAP.Y", 0))
        self.W = int(d.get("MAP.WIDTH", 64)); self.H = int(d.get("MAP.HEIGHT", 64))
        t = theater(root)
        raw = open(os.path.join(EX, "MAP", s + ".MAP"), "rb").read()
        ids = [struct.unpack_from(">H", raw, i * 2)[0] for i in range(4096)]
        self.tmpl, self.icon = [], []
        for tid in ids:
            if tid < t["n4"]:
                self.tmpl.append(t["tl4"][2 * tid]); self.icon.append(t["tl4"][2 * tid + 1])
            elif 1024 <= tid < 1024 + t["n8"]:
                k = tid - 1024
                self.tmpl.append(t["tl8"][2 * k]); self.icon.append(t["tl8"][2 * k + 1])
            else:
                self.tmpl.append(255); self.icon.append(0)
        blob = open(os.path.join(EX, "IMG", s + ".IMG"), "rb").read()
        hdr, _b, w, h = struct.unpack_from(">IIHH", blob, 0)
        assert (hdr, w, h) == (0x10, 65, 65), s
        self.h = blob[16:16 + 4225]

    def cell(self, cx, cy):
        i = cy * 65 + cx
        return (self.h[i], self.h[i + 1], self.h[i + 65], self.h[i + 66])


def placements(s):
    """Every SLOPE placement on this map, recovered the way the editor recovers one:
       an origin cell carrying icon 0, then the cells of its own w x h box that carry
       this template and the icon that box position implies."""
    out = []
    for cy in range(64):
        for cx in range(64):
            t = s.tmpl[cy * 64 + cx]
            if not (13 <= t <= 50) or s.icon[cy * 64 + cx] != 0:
                continue
            nm, w, h = WH[t]
            cells = []
            for dy in range(h):
                for dx in range(w):
                    x, y = cx + dx, cy + dy
                    if x >= 64 or y >= 64:
                        continue
                    if s.tmpl[y * 64 + x] == t and s.icon[y * 64 + x] == dy * w + dx:
                        cells.append((x, y))
            out.append((t, nm, cx, cy, w, h, cells))
    return out


def main():
    scens = [f[:-4] for f in sorted(os.listdir(os.path.join(EX, "MAP")))
             if f.endswith(".MAP") and
             os.path.exists(os.path.join(EX, "IMG", f[:-4] + ".IMG"))]
    tot = right = back = level = 0
    per = collections.defaultdict(lambda: [0, 0, 0, 0])
    inplay = 0
    for sid in scens:
        s = Scen(sid)
        for (t, nm, ox, oy, w, h, cells) in placements(s):
            if not cells:
                continue
            # inside the playable rectangle only: outside it the heights are noise
            if not (s.X <= ox and ox + w <= s.X + s.W and
                    s.Y <= oy and oy + h <= s.Y + s.H):
                continue
            inplay += 1
            gx = gy = 0.0
            for (x, y) in cells:
                nw, ne, sw, se = s.cell(x, y)
                gx += ((ne + se) - (nw + sw)) / 2.0
                gy += ((sw + se) - (nw + ne)) / 2.0
            gx /= len(cells); gy /= len(cells)
            ux, uy = DIR[HIGH[t - 12]]
            n = math.hypot(ux, uy)
            proj = (gx * ux + gy * uy) / n
            tot += 1
            p = per[nm]
            p[0] += 1
            if abs(proj) < LEVEL:
                level += 1; p[3] += 1
            elif proj > 0:
                right += 1; p[1] += 1
            else:
                back += 1; p[2] += 1
    print(f"{len(scens)} heightmapped scenarios")
    print(f"{inplay} SLOPE placements wholly inside the playable rectangle\n")
    print(f"  agrees with the compass   {right:5d}  {100.0*right/tot:5.2f}%")
    print(f"  points the other way      {back:5d}  {100.0*back/tot:5.2f}%")
    print(f"  level (|grad| < {LEVEL} bytes) {level:5d}  {100.0*level/tot:5.2f}%")
    print(f"  total                     {tot:5d}\n")
    print(f"  {'template':9s} {'n':>5s} {'right':>6s} {'back':>5s} {'level':>6s}")
    for nm in sorted(per, key=lambda k: int(k[1:])):
        p = per[nm]
        print(f"  {nm:9s} {p[0]:5d} {p[1]:6d} {p[2]:5d} {p[3]:6d}")


main()
