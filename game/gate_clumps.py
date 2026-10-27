#!/usr/bin/env python3
"""G344: the tree clumps drawn whole, and T16's and T08's own temperate trees.

  gate_clumps.py script DIR    write the run scripts into DIR
  gate_clumps.py check DIR     read the runs' logs and pictures; first line is
                               "1 message" or "0 message"

Under Enhanced a tree clump (TC01 to TC05) with a model of its own (game/ebld/TC0N_TEM.ebld:
the approved trees composed as the 1995 art pastes them, tools/art/remaster/foliage/
compose_clump.py) is drawn whole, that one model at the clump's centre coordinate, where
the renderer used to plant a single tree on each blocked cell. Two original temperate
missions, every clump and every T16 and T08 they place visited by the camera:

  e    Nod 4A (SCG04EA, all five clumps and T08) Enhanced, each clump and tree in turn;
  n    the same first view with --noclumptrees (the cartridge's own picture: no clump drawn);
  t    GDI 1 (SCG01EA, T16 and four of the clumps) Enhanced, each in turn;
  s    the same with --noebldshare (every file its own copy of each texture on the card).

Claims:
  (1) every clump on the map (a blocked cell inside the mission's rectangle) is drawn whole
      once the camera visits it, once each (a CLUMP line per instance),
      its model's anchor, read back out of draw_mesh, on the engine's centre coordinate,
      and that coordinate the clump's own: its top-left cell plus the type's centre base
      (tdata.cpp, 1995 pixels at 24 a cell) within a lepton's rounding;
  (2) no clump is composed of single trees (the draw line's composed count is 0) and each
      of the five types was drawn from its own file with its wind (EBLDART TC0N_TEM drawn
      and wind-on draws);
  (3) with --noclumptrees no clump is drawn and the first frame differs from Enhanced's by
      at least 3000 pixels left of the sidebar;
  (4) T16 is drawn from its own file (T16_TEM, no longer T01's) and T08 from its temperate
      look (T08_TEM), both with their wind;
  (5) the clumps' textures are their member trees' own, put on the card once: each clump
      type GDI 1 draws shares at least one texture with another tree's file, the
      remastered textures on the card come to under 60 per cent of --noebldshare's, and
      both of the run's frames are byte for byte --noebldshare's.
"""
import os
import re
import sys

# tdata.cpp's centre base of each clump (XYP_COORD, 1995 pixels from the art's top left)
CENTRE_BASE = {"TC01": (28, 41), "TC02": (38, 41), "TC03": (33, 35), "TC04": (44, 49),
               "TC05": (49, 58)}
# the cells each clump blocks (the engine's occupy lists, cnc_eyes.cpp CLUMPS)
OCC = {"TC01": ((0, 1), (1, 1)), "TC02": ((1, 0), (0, 1), (1, 1)),
       "TC03": ((0, 0), (1, 0), (0, 1), (1, 1)), "TC04": ((0, 1), (1, 1), (2, 1), (0, 2)),
       "TC05": ((2, 0), (0, 1), (1, 1), (2, 1), (1, 2), (2, 2))}
RUNS = {"e": ("SCG04EA", ""), "n": ("SCG04EA", "--noclumptrees"), "t": ("SCG01EA", ""),
        "s": ("SCG01EA", "--noebldshare")}
WANT = {"e": ("TC01", "TC02", "TC03", "TC04", "TC05", "T08"), "t": ("TC01", "TC02", "TC04",
                                                                  "TC05", "T16")}
WANT["s"] = WANT["t"]


def placed(mission, types):
    """(type, x, y) of every terrain object of these types the mission's INI places."""
    path = os.path.join("missions", mission + ".INI")
    sec = open(path, encoding="latin-1").read().split("[TERRAIN]")[1].split("[")[0]
    out = []
    for line in sec.splitlines():
        m = re.match(r"\s*(\d+)=(\w+),", line)
        if m and m.group(2) in types:
            c = int(m.group(1))
            out.append((m.group(2), c % 64, c // 64))
    return out


def map_rect(mission):
    """(x0, y0, x1, y1) of the mission's playable rectangle, its [MAP] section."""
    text = open(os.path.join("missions", mission + ".INI"), encoding="latin-1").read()
    sec = text.split("[MAP]")[1].split("[")[0]
    v = {k.strip(): int(val) for k, val in (l.split("=", 1) for l in sec.splitlines()
                                            if "=" in l and l.split("=")[0].strip() in
                                            ("X", "Y", "Width", "Height"))}
    return v["X"], v["Y"], v["X"] + v["Width"], v["Y"] + v["Height"]


def on_map(t, x, y, rect):
    """Whether a clump blocks a cell inside the map (one outside it is never drawn)."""
    x0, y0, x1, y1 = rect
    return any(x0 <= x + dx < x1 and y0 <= y + dy < y1 for dx, dy in OCC[t])


def camera(t, x, y):
    if t in CENTRE_BASE:
        bx, by = CENTRE_BASE[t]
        return x + bx / 24.0, y + by / 24.0
    return x + 1.0, y + 1.0


def script(d):
    os.makedirs(d, exist_ok=True)
    for tag, (mission, _) in RUNS.items():
        objs = placed(mission, WANT.get(tag, WANT["e"]))
        s = ["tick 3", "dist 1600"]
        first = camera(*objs[0])
        s += ["cam %.2f %.2f" % first, "tick 1", "shot %s/%s_first.png" % (d, tag)]
        if tag != "n":
            # (a frame is drawn at a shot, so each visit takes one, into one scratch file)
            for t, x, y in objs:
                s += ["cam %.2f %.2f" % camera(t, x, y), "tick 1",
                      "shot %s/%s_visit.png" % (d, tag)]
        s += ["ebldump", "quit"]
        with open(os.path.join(d, tag + ".txt"), "w") as f:
            f.write("\n".join(s) + "\n")


def read(path):
    try:
        return open(path, errors="replace").read().splitlines()
    except OSError:
        return []


def kv(line):
    return dict(p.split("=", 1) for p in line.split("|") if "=" in p)


def arts(lines):
    out = {}
    for l in lines:
        if l.startswith("EBLDART|"):
            out[l.split("|")[1]] = kv(l)        # (the last dump wins)
    return out


def check(d):
    why, notes = [], []
    e, n, t, sh = (read(os.path.join(d, k + ".log")) for k in ("e", "n", "t", "s"))
    if not e or not n or not t or not sh:
        return "0 a run left no log"
    # (1) every visited clump drawn whole on its own centre coordinate
    for tag, lines in (("e", e), ("t", t)):
        mission = RUNS[tag][0]
        rect = map_rect(mission)
        clumps = [o for o in placed(mission, CENTRE_BASE) if o[0] in WANT[tag]
                  and on_map(o[0], o[1], o[2], rect)]
        seen = {}
        for l in lines:
            if l.startswith("CLUMP|"):
                f = l.split("|")
                seen.setdefault((f[1], f[2]), []).append(kv(l))
        for ty, x, y in clumps:
            got = seen.get((ty, "cell=%d,%d" % (x, y)), [])
            if len(got) != 1:
                why.append("%s %s at %d,%d drawn whole %d times" % (mission, ty, x, y, len(got)))
                continue
            cx, cz = map(float, got[0]["centre"].split(","))
            ax, az = map(float, got[0]["anchor"].split(","))
            bx, by = CENTRE_BASE[ty]
            if abs(ax - cx) > 0.002 or abs(az - cz) > 0.002:
                why.append("%s %s at %d,%d anchored at %.3f,%.3f, its centre %.3f,%.3f"
                           % (mission, ty, x, y, ax, az, cx, cz))
            elif abs(cx - (x + bx / 24.0)) > 0.01 or abs(cz - (y + by / 24.0)) > 0.01:
                why.append("%s %s at %d,%d: centre %.3f,%.3f, the art's %.3f,%.3f"
                           % (mission, ty, x, y, cx, cz, x + bx / 24.0, y + by / 24.0))
        notes.append("%s: %d clumps whole" % (mission, len(clumps)))
    # (2) none composed, each type from its own file with its wind
    for tag, lines in (("e", e), ("t", t)):
        comp = [l for l in lines if l.startswith("draw: ") and "composed clumps" in l]
        m = re.search(r"(\d+) composed clumps", comp[-1]) if comp else None
        if not m or int(m.group(1)) != 0:
            why.append("%s: composed clumps %s" % (RUNS[tag][0], m.group(1) if m else "?"))
    a = arts(e)
    for ty in sorted(CENTRE_BASE):
        r = a.get(ty + "_TEM")
        if not r or int(r.get("drawn", "0")) <= 0 or int(r.get("wind", "0")) <= 0 \
           or int(r.get("winddrawn", "0")) <= 0:
            why.append("%s_TEM not drawn with its wind (%s)" % (ty, r and {k: r.get(k) for k in
                                                                          ("drawn", "wind", "winddrawn")}))
    # (3) --noclumptrees draws no clump, and the picture differs
    if any(l.startswith("CLUMP|") for l in n):
        why.append("--noclumptrees drew a clump")
    an = arts(n)
    if any(int(an.get(ty + "_TEM", {}).get("drawn", "0")) > 0 for ty in CENTRE_BASE):
        why.append("--noclumptrees drew a clump's model")
    try:
        from PIL import Image, ImageChops
        pe = Image.open(os.path.join(d, "e_first.png")).convert("RGB")
        pn = Image.open(os.path.join(d, "n_first.png")).convert("RGB")
        w, h = pe.size
        diff = ImageChops.difference(pe.crop((0, 0, int(w * 0.8), h)),
                                     pn.crop((0, 0, int(w * 0.8), h))).convert("L")
        px = sum(diff.histogram()[9:])
        if px < 3000:
            why.append("the clumps' frame differs from --noclumptrees by %d px" % px)
        notes.append("%d px against --noclumptrees" % px)
    except (OSError, ImportError) as ex:
        why.append("pictures: %s" % ex)
    # (4) T16 and T08 from their own temperate files
    at = arts(t)
    for name, src in (("T16_TEM", at), ("T08_TEM", a)):
        r = src.get(name)
        if not r or int(r.get("drawn", "0")) <= 0 or int(r.get("winddrawn", "0")) <= 0:
            why.append("%s not drawn with its wind" % name)
    # (5) the clumps' textures shared with their members' files, the pictures unchanged
    def card_mb(lines):
        v = [float(m.group(1)) for l in lines
             for m in [re.search(r"\(([0-9.]+) MB in all\)", l)] if m and "textures on the card" in l]
        return v[-1] if v else -1.0
    for ty in ("TC01", "TC02", "TC04", "TC05"):
        lines = [l for l in t if l.startswith("ebld: %s textures on the card" % ty)]
        if not lines or "shared)" not in lines[0]:
            why.append("%s shares no texture with its members' files" % ty)
    mb, mb0 = card_mb(t), card_mb(sh)
    if mb < 0 or mb0 <= 0 or mb > 0.6 * mb0:
        why.append("textures on the card %.1f MB shared against %.1f MB not" % (mb, mb0))
    notes.append("textures on the card %.1f MB, %.1f MB without sharing" % (mb, mb0))
    try:
        from PIL import Image, ImageChops
        for f in ("first", "visit"):
            a = Image.open(os.path.join(d, "t_%s.png" % f)).convert("RGB")
            b = Image.open(os.path.join(d, "s_%s.png" % f)).convert("RGB")
            if ImageChops.difference(a, b).getbbox() is not None:
                why.append("the %s frame differs from --noebldshare's" % f)
    except OSError as ex:
        why.append("pictures: %s" % ex)
    if why:
        return "0 " + "; ".join(why[:6])
    return "1 the tree clumps drawn whole: " + ", ".join(notes) + \
        "; each type from its own file with its wind; T16 and T08 from their temperate files;" \
        " the frames byte for byte those without sharing"


if __name__ == "__main__":
    if len(sys.argv) != 3 or sys.argv[1] not in ("script", "check"):
        sys.exit(__doc__)
    if sys.argv[1] == "script":
        script(sys.argv[2])
    else:
        print(check(sys.argv[2]))
