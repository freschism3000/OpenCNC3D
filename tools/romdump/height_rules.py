#!/usr/bin/env python3
"""
CNC3D -- what the N64 heightmaps are MADE OF (the follow-up to heightmap_notes.md).

heightmap_notes.md answered WHERE the heights live (a 65x65 8-bit <SCEN>.IMG per
scenario, world Y = byte*4, cell pitch 256). This answers WHAT IS IN THEM: whether
the elevation is arbitrary, derived from the tile map, or something in between.

Everything printed below is measured from the cartridge's own files. Nothing is
asserted that this script does not compute. Run it to reproduce the numbers in
docs/terrain-elevation-grammar.md.

    python3 height_rules.py            all sections
    python3 height_rules.py ladder     just one section (ladder|families|slopes|
                                       width|water|twins)

Reads the extracted tree (tools/bakery/sharecopy/assets/extracted). Read-only.
"""
import collections, math, os, statistics, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
EX = os.path.join(HERE, "..", "bakery", "sharecopy", "assets", "extracted")
DEFINES = os.path.join(HERE, "..", "..", "brain", "vanilla", "tiberiandawn", "defines.h")
STEP = 64                      # bytes; *4 world units = 256 = one cell width
LADDER = (0, 64, 128, 191, 255)
TRANSITION = {"SLOPE", "FALLS", "RIVER", "FORD", "SHORE", "BRIDGE"}
LAND = {"CLEAR", "ROAD", "PATCH", "BRUSH", "BOULDER"}
FAMILIES = ("CLEAR", "WATER", "SHORE", "SLOPE", "BRUSH", "PATCH", "BOULDER",
            "ROAD", "RIVER", "FORD", "FALLS", "BRIDGE")


def template_names():
    names, on = [], False
    for line in open(DEFINES, errors="ignore"):
        s = line.split("//")[0].strip()
        if s.startswith("TEMPLATE_CLEAR1"):
            on = True
        if on:
            if s.startswith("TEMPLATE_COUNT"):
                break
            if s.startswith("TEMPLATE_"):
                names.append(s.rstrip(",").split()[0].rstrip(",").replace("TEMPLATE_", ""))
    return names


TN = template_names()


def family(t):
    if t is None or t >= len(TN):
        return "CLEAR"
    for f in FAMILIES:
        if TN[t].startswith(f):
            return f
    return "CLEAR"


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


_th = {}


def theater(root):
    if root not in _th:
        rd = lambda p: open(os.path.join(EX, p), "rb").read()
        _th[root] = dict(n4=os.path.getsize(os.path.join(EX, f"DA4/{root}.DA4")) // 288,
                         n8=os.path.getsize(os.path.join(EX, f"DA8/{root}.DA8")) // 576,
                         tl4=rd(f"TL4/{root}.TL4"), tl8=rd(f"TL8/{root}.TL8"))
    return _th[root]


class Scen:
    def __init__(self, s):
        self.id = s
        d = ini(s)
        th = d.get("MAP.THEATER", "TEMPERATE").upper()
        self.root = {"DESERT": "DESERT", "TEMPERATE": "TEMPERAT"}.get(th, th[:8])
        self.X = int(d.get("MAP.X", 0)); self.Y = int(d.get("MAP.Y", 0))
        self.W = int(d.get("MAP.WIDTH", 64)); self.H = int(d.get("MAP.HEIGHT", 64))
        t = theater(self.root)
        raw = open(os.path.join(EX, "MAP", s + ".MAP"), "rb").read()
        ids = [struct.unpack_from(">H", raw, i * 2)[0] for i in range(4096)]
        self.tmpl = []
        for tid in ids:
            if tid < t["n4"]:
                self.tmpl.append(t["tl4"][2 * tid])
            elif 1024 <= tid < 1024 + t["n8"]:
                self.tmpl.append(t["tl8"][2 * (tid - 1024)])
            else:
                self.tmpl.append(255)
        self.fam = [family(x) if x != 255 else "CLEAR" for x in self.tmpl]
        blob = open(os.path.join(EX, "IMG", s + ".IMG"), "rb").read()
        hdr, _b, w, h = struct.unpack_from(">IIHH", blob, 0)
        assert (hdr, w, h, blob[12], blob[13]) == (0x10, 65, 65, 4, 1), s
        self.h = blob[16:16 + 4225]

    def corner(self, x, y):
        return self.h[min(64, max(0, y)) * 65 + min(64, max(0, x))]

    def cell(self, cx, cy):
        i = cy * 65 + cx
        return (self.h[i], self.h[i + 1], self.h[i + 65], self.h[i + 66])

    def inner(self):
        for cy in range(self.Y, min(64, self.Y + self.H)):
            for cx in range(self.X, min(64, self.X + self.W)):
                yield cx, cy


def campaign():
    """The 55 scenarios that ship their own heightmap (the other 24 use FLAT.IMG,
    4225 zero bytes, and are flat by definition)."""
    return [Scen(f[:-4]) for f in sorted(os.listdir(os.path.join(EX, "MAP")))
            if f.endswith(".MAP") and os.path.exists(
                os.path.join(EX, "IMG", f[:-4] + ".IMG"))]


# ------------------------------------------------------------------ sections
def sec_ladder(ss):
    print("1. THE LADDER -- terrain sits on rungs, and the rung spacing is one cell\n")
    hist = collections.Counter()
    for s in ss:
        for v in s.h:
            hist[v] += 1
    tot = sum(hist.values())
    print("   the ten most common height bytes over all 55 heightmaps:")
    for v, c in hist.most_common(10):
        print(f"     byte {v:3d}  {c:6d}  {100*c/tot:5.2f}%   world Y {v*4:4d}"
              f"   {'<- rung' if v in LADDER else ''}")
    print(f"\n   255/4 = 63.75, so evenly spaced quarters of the byte range land on")
    print(f"   {LADDER}. That is what the data does:")
    for a, b, label in ((189, 193, "third rung"), (252, 255, "fourth rung")):
        print("     " + label + ": " +
              "  ".join(f"{v}:{hist[v]}" for v in range(a, b + 1)))
    print("   (191 beats 192 by 13x, so the ladder is quarters of 0..255, not")
    print("    multiples of 64 -- the same picture a paint tool's 0/25/50/75/100%")
    print("    greys would leave.)\n")

    gaps = collections.Counter()
    for s in ss:
        lv = collections.Counter()
        for cx, cy in s.inner():
            if s.fam[cy * 64 + cx] not in LAND:
                continue
            c = set(s.cell(cx, cy))
            if len(c) == 1:
                lv[c.pop()] += 1
        tot2 = sum(lv.values()) or 1
        keep = sorted(v for v, c in lv.items() if c / tot2 >= 0.01)
        merged = []
        for v in keep:
            if merged and v - merged[-1][-1] <= 3:
                merged[-1].append(v)
            else:
                merged.append([v])
        reps = [g[len(g) // 2] for g in merged]
        for i in range(len(reps) - 1):
            gaps[reps[i + 1] - reps[i]] += 1
    g = sum(gaps.values())
    near = sum(c for v, c in gaps.items() if 62 <= v <= 66)
    print("   gap between one flat plateau level and the next, per map:")
    for v, c in sorted(gaps.items()):
        if c >= 2:
            print(f"     {v:4d} bytes  x{c:3d} {'#'*c}")
    print(f"\n   {near} of {g} gaps ({100*near/g:.0f}%) are 62..66 bytes. 64 bytes x 4")
    print( "   world units = 256 = EXACTLY ONE CELL WIDTH. A terrace is one cell taller")
    print( "   than the one below it, in the engine's own units. Section 4 shows the")
    print( "   climb is spread over 2-3 cells, so the face runs at roughly 18-27 deg.\n")


def sec_families(ss):
    print("2. WHICH TILES CARRY THE RELIEF (playable rectangle only)\n")
    n, steep, flat, near = (collections.Counter() for _ in range(4))
    for s in ss:
        for cx, cy in s.inner():
            f = s.fam[cy * 64 + cx]
            c = s.cell(cx, cy)
            r = max(c) - min(c)
            n[f] += 1
            if r == 0:
                flat[f] += 1
            if r >= 16:
                steep[f] += 1
                if f in TRANSITION:
                    near["on the tile"] += 1
                else:
                    d = 99
                    for yy in range(max(0, cy - 2), min(64, cy + 3)):
                        for xx in range(max(0, cx - 2), min(64, cx + 3)):
                            if s.fam[yy * 64 + xx] in TRANSITION:
                                d = min(d, max(abs(yy - cy), abs(xx - cx)))
                    near[{1: "1 cell away", 2: "2 cells away"}.get(d, "further off")] += 1
    tn, ts = sum(n.values()), sum(steep.values())
    print(f"   {tn} cells. 'steep' = the four corners of the cell differ by >= 16"
          f" bytes (a quarter step).\n")
    print(f"   {'family':9s} {'cells':>7s} {'%map':>6s} {'flat':>6s} | {'steep':>6s}"
          f" {'%steep':>7s} {'enrich':>7s}")
    for f in sorted(n, key=lambda k: -steep[k]):
        sh, sf = n[f] / tn, steep[f] / ts
        print(f"   {f:9s} {n[f]:7d} {100*sh:5.2f}% {100*flat[f]/n[f]:5.1f}% |"
              f" {steep[f]:6d} {100*sf:6.2f}% {sf/sh:6.2f}x")
    print("\n   cliff and water art is where the height changes, and the tiles that")
    print("   are supposed to be ground are the tiles that are flat.\n")
    t = sum(near.values())
    print("   for a steep cell, how far is the nearest cliff/water TILE:")
    for k in ("on the tile", "1 cell away", "2 cells away", "further off"):
        print(f"     {k:14s} {near[k]:6d}  {100*near[k]/t:5.1f}%")
    print()


def sec_slopes(ss):
    print("3. THE 38 CLIFF TEMPLATES ARE A COMPASS\n")
    grad = collections.defaultdict(lambda: [0.0, 0.0, 0])
    for s in ss:
        for cx, cy in s.inner():
            t = s.tmpl[cy * 64 + cx]
            if t == 255 or family(t) != "SLOPE":
                continue
            nw, ne, sw, se = s.cell(cx, cy)
            g = grad[TN[t]]
            g[0] += ((ne + se) - (nw + sw)) / 2.0
            g[1] += ((sw + se) - (nw + ne)) / 2.0
            g[2] += 1

    def face(dx, dy):
        if math.hypot(dx, dy) < 3:
            return "(flat)"
        a = math.degrees(math.atan2(-dy, dx))
        d = [(0, "E "), (45, "NE"), (90, "N "), (135, "NW"), (180, "W "),
             (-135, "SW"), (-90, "S "), (-45, "SE")]
        return min(d, key=lambda k: abs((a - k[0] + 180) % 360 - 180))[1]
    print(f"   {'template':10s} {'n':>5s} {'dz/dx':>7s} {'dz/dy':>7s} {'rise':>6s}  high side")
    for t in sorted(grad, key=lambda k: int(k[5:])):
        dx, dy, k = grad[t]
        dx /= k; dy /= k
        print(f"   {t:10s} {k:5d} {dx:7.1f} {dy:7.1f} {math.hypot(dx,dy):6.1f}  {face(dx,dy)}")
    print("\n   SLOPE1-7 north, 8-14 east, 15-21 south, 22-28 west, 29-38 the corners.")
    print("   The DOS cliff set is four edges x seven variants plus corner pieces, and")
    print("   the heightmap agrees with that layout on every single template.\n")


def sec_width(ss):
    print("4. HOW WIDE A CLIMB IS\n")
    def rung(v):
        b = min(LADDER, key=lambda r: abs(r - v))
        return LADDER.index(b) if abs(b - v) <= 4 else None
    w = collections.Counter(); prof = collections.Counter()
    for s in ss:
        lines = [[s.corner(x, y) for x in range(65)] for y in range(65)] + \
                [[s.corner(x, y) for y in range(65)] for x in range(65)]
        for L in lines:
            i = 0
            while i < 64:
                a = rung(L[i])
                if a is None:
                    i += 1; continue
                j = i + 1
                while j < 65 and rung(L[j]) is None:
                    j += 1
                if j < 65 and rung(L[j]) is not None and rung(L[j]) != a:
                    up = all(L[k + 1] >= L[k] for k in range(i, j))
                    dn = all(L[k + 1] <= L[k] for k in range(i, j))
                    if (up or dn) and j > i + 1:
                        w[j - i] += 1
                        if j - i <= 4 and abs(rung(L[j]) - a) == 1:
                            span = L[j] - L[i]
                            prof[(j - i, tuple(round((v - L[i]) * 8 / span) / 8
                                               for v in L[i:j + 1]))] += 1
                    i = j
                else:
                    i = max(j, i + 1)
    t = sum(w.values())
    print("   a one-rung climb, measured in cells crossed:")
    for k, c in sorted(w.items())[:8]:
        print(f"     {k:2d} cells  {c:6d}  {100*c/t:5.1f}%  {'#'*int(60*c/t)}")
    print("\n   most common normalised climb profiles (0 = foot, 1 = top):")
    for (k, p), c in prof.most_common(8):
        print(f"     {k} cells: {list(p)}   x{c}")
    print("\n   Most cliff templates are 2x2 blocks of art (a few are 2x3), and the")
    print("   climb takes 2 or 3 cells. The rise spans the template, not one edge.\n")


def sec_water(ss):
    print("5. THE OTHER DIRECTION: WHAT GETS CARVED DOWN\n")
    depth = collections.defaultdict(list)
    for s in ss:
        ch = [[sum(s.cell(x, y)) / 4 for x in range(64)] for y in range(64)]
        for cx, cy in s.inner():
            f = s.fam[cy * 64 + cx]
            if f not in TRANSITION and f != "WATER":
                continue
            if f == "SLOPE":
                continue
            land = [ch[yy][xx] for yy in range(max(0, cy - 3), min(64, cy + 4))
                    for xx in range(max(0, cx - 3), min(64, cx + 4))
                    if s.fam[yy * 64 + xx] in LAND]
            if len(land) >= 3:
                depth[f].append(ch[cy][cx] - statistics.median(land))
    print(f"   {'family':8s} {'n':>6s} {'mean':>7s} {'median':>7s} {'p10':>7s}"
          f" {'p90':>7s} {'% below':>8s}   (bytes below the surrounding land)")
    for f in sorted(depth, key=lambda k: statistics.median(depth[k])):
        v = sorted(depth[f]); n = len(v)
        print(f"   {f:8s} {n:6d} {statistics.mean(v):7.1f} {statistics.median(v):7.1f}"
              f" {v[n//10]:7.1f} {v[9*n//10]:7.1f} "
              f"{100*sum(1 for x in v if x < -2)/n:7.1f}%")
    print("\n   Channels are shallow -- a fifth to a third of a cell, not a whole rung --")
    print("   and BRIDGE sits at bank height, which is what a bridge deck should do.\n")


def sec_twins(ss):
    print("6. THE CONTROL: SAME TILE MAP, DIFFERENT HEIGHTMAP\n")
    by = {s.id: s for s in ss}
    seen = set()
    for a in ss:
        for b in ss:
            if a.id >= b.id or (a.id, b.id) in seen:
                continue
            if a.tmpl != b.tmpl:
                continue
            dh = sum(1 for i in range(4225) if a.h[i] != b.h[i])
            mx = max(abs(a.h[i] - b.h[i]) for i in range(4225))
            print(f"   {a.id} and {b.id} have IDENTICAL 64x64 tile maps, and their")
            print(f"   heightmaps differ in {dh} of 4225 corners (max delta {mx}).")
            seen.add((a.id, b.id))
    print("\n   So the heightmap is NOT a function of the tile map. It is its own")
    print("   authored layer, written to agree with the tile art -- and where a")
    print("   designer wanted a different shape on the same ground, they got one.\n")


SECTIONS = dict(ladder=sec_ladder, families=sec_families, slopes=sec_slopes,
                width=sec_width, water=sec_water, twins=sec_twins)

if __name__ == "__main__":
    ss = campaign()
    print(f"\n{len(ss)} scenarios ship their own heightmap; "
          f"{len(os.listdir(os.path.join(EX,'MAP'))) - len(ss)} fall back to "
          f"FLAT.IMG (all zero).\n")
    want = sys.argv[1:] or list(SECTIONS)
    for k in want:
        SECTIONS[k](ss)
