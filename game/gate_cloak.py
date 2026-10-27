#!/usr/bin/env python3
"""G333's two halves: the scene it stages and the claims it checks.

    python3 gate_cloak.py maps DIR           write the test mission and the run's script
                                             (cloak.txt)
    python3 gate_cloak.py check LOG SHOTDIR  read the run's log and shots (g333_*.png) and
                                             print "1 message" or "0 why"

WHAT IS UNDER TEST. Classic (gfxmode 0) draws a Stealth Tank as the 1995 engine does
through its whole cloaking cycle (the CLOAKING block and dos2d_visual / dos2d_shimmer in
dosclassic_mod.h). Somebody else's fully cloaked tank is not drawn at all. The layer
export cannot say so by itself: Techno_Draw_Object hands it a hidden unit's layers as if
the unit were only shadowy, and Classic drew those layers as two copies of the tank.

THE SCENE. A copy of the skirmish map SCM01EA as a single-player mission, SCG99EA: one Nod
Stealth Tank and one of the player's own (GDI), ten cells apart on open grass, both in
the one Classic view, the Nod tank left of its middle and the GDI tank right of it, so the
shimmer's two step tables are both drawn. Both start to cloak on their own and are fully
cloaked by frame 40. Neither fires: the player's stays on guard without a target, as a
human's cloaking unit does, and the other is out of its range.

THE SCRIPT stands on frames: frame 1 to frame FRAMES, each shot twice, with both tanks
drawn and with every layer of type STNK left out (`dosomit STNK`), so what Classic drew
for each tank is the difference of two pictures of one frame. `doscloakprobe` after each
first shot prints the engine's cloak state, the device's stage and draw flags for every
layer of both tanks, the visual character worked out from them, and each tank's
rectangle.
"""
import os
import re
import sys

NOD = (12, 28)
GDI = (20, 34)
CAM = (16.5, 31.5)
FRAMES = 46
# the layers Techno_Draw_Object gives the export for each visual character, as draw flags
# (FADING|GHOST 0x1130, FADING|PREDATOR 0x330, PREDATOR 0x230), in draw order
LAYERS = {"normal": [4400], "indistinct": [4400, 560], "darken": [4400, 816, 560],
          "shadowy": [816, 560], "ripple": [560], "hidden": [816, 560]}
TAIL = ["", "[TERRAIN]", "", "[SMUDGE]", "", "[Triggers]", "", "[CellTriggers]", "",
        "[Teams]", "", "[Waypoints]", "", "[Base]", "Count=0", ""]


def cell(x, y):
    return y * 64 + x


def maps(out):
    src = os.path.join("missions", "SCM01EA")
    b = open(src + ".BIN", "rb").read()
    ini = open(src + ".INI", encoding="latin-1").read()
    mapsec = re.search(r"\[MAP\].*?(?=\n\[)", ini, re.S).group(0).replace("\r", "").strip()
    os.makedirs(out, exist_ok=True)
    un = ["000=BadGuy,STNK,256,%d,0,Guard,None" % cell(*NOD),
          "001=GoodGuy,STNK,256,%d,0,Guard,None" % cell(*GDI)]
    text = ["[Basic]", "Player=GoodGuy", "Action=x", "Lose=x", "Win=x", "Brief=x", "Intro=x",
            "BuildLevel=98", "Theme=No theme", "Percent=0", "CarryOverCap=-1",
            "CarryOverMoney=0", "", mapsec, "",
            "[GoodGuy]", "Allies=GoodGuy", "MaxBuilding=150", "MaxUnit=150",
            "Credits=0", "Edge=South", "", "[BadGuy]", "Allies=BadGuy", "MaxBuilding=150",
            "MaxUnit=150", "Credits=0", "Edge=North", "",
            "[STRUCTURES]", "", "[UNITS]"] + un + TAIL
    open(os.path.join(out, "SCG99EA.INI"), "w", encoding="latin-1",
         newline="").write("\r\n".join(text))
    open(os.path.join(out, "SCG99EA.BIN"), "wb").write(b)
    s = ["gfxmode 0", "cam %.1f %.1f" % CAM]
    for f in range(1, FRAMES + 1):
        s += ["tick 1", "shot shots/g333_f%02d_on.png" % f, "doscloakprobe",
              "dosomit STNK", "shot shots/g333_f%02d_off.png" % f, "dosomit -"]
    s += ["quit"]
    open(os.path.join(out, "cloak.txt"), "w").write("\n".join(s) + "\n")


def rd(path):
    try:
        with open(path, errors="ignore") as f:
            return f.read().splitlines()
    except OSError:
        return []


def field(l, k):
    m = re.search(r"\|%s=([^|]*)" % k, l)
    return m.group(1) if m else None


def probes(lines):
    """one entry per doscloakprobe: the engine frame and, per tank (ours 0 or 1), its
    layers in draw order"""
    out, cur = [], None
    for l in lines:
        if l.startswith("DOSCLOAK|frame|"):
            cur = {"frame": int(field(l, "engineframe")), "tanks": {}}
            out.append(cur)
        elif l.startswith("DOSCLOAK|STNK|") and cur is not None:
            t = cur["tanks"].setdefault(int(field(l, "ours")), [])
            t.append({k: field(l, k) for k in
                      ("id", "sub", "owner", "cloak", "cstage", "visual", "flags", "as")})
            t[-1]["rect"] = [float(v) for v in field(l, "rect").split(",")]
    return out


def check(log, shots):
    from PIL import Image
    import numpy as np
    why = []

    def need(c, m):
        if not c:
            why.append(m)
        return c

    def img(f, which):
        p = os.path.join(shots, "g333_f%02d_%s.png" % (f, which))
        return np.asarray(Image.open(p).convert("RGB")).astype(np.int64) if os.path.exists(p) else None

    P = probes(rd(log))
    if not need(len(P) == FRAMES, "%d probes in the log, want %d" % (len(P), FRAMES)):
        return "0 " + "; ".join(why)

    # (1) ENGINE TRUTH. Both tanks are in the view on every frame, each runs UNCLOAKED,
    # CLOAKING (its stage rising by one a frame), CLOAKED, and on every frame the layers the
    # engine exported are the ones Techno_Draw_Object gives the character Classic worked
    # out, in the same order: Classic's ladder is the engine's own. For a hidden tank the
    # export still carries two layers, which is what there was to draw by mistake.
    states = {0: [], 1: []}
    for i, p in enumerate(P):
        need(p["frame"] == i + 1, "probe %d is of engine frame %d" % (i + 1, p["frame"]))
        for ours in (0, 1):
            t = p["tanks"].get(ours)
            if not need(t, "frame %d: no layers of the %s tank in the view"
                        % (i + 1, "player's" if ours else "Nod")):
                states[ours].append(None)
                continue
            v = t[0]["visual"]
            states[ours].append((int(t[0]["cloak"]), int(t[0]["cstage"]), v))
            flags = [int(l["flags"]) for l in t]
            if need(sorted(flags) == sorted(LAYERS.get(v, [])),
                    "frame %d, %s tank: visual %s but the engine exported flags %s"
                    % (i + 1, "player's" if ours else "Nod", v, flags)):
                # and Classic draws them in Techno_Draw_Object's order: a darkening tank's
                # darkening goes over the tank, not under it as an aircraft's shadow does
                need(flags == LAYERS[v],
                     "frame %d, %s %s tank: Classic draws its layers in the order %s, the "
                     "1995 engine %s" % (i + 1, "player's" if ours else "Nod", v, flags,
                                         LAYERS[v]))
            need(len(set((l["id"], l["owner"], l["cloak"]) for l in t)) == 1
                 and t[0]["owner"] == ("0" if ours else "1"),
                 "frame %d: the %s tank's layers disagree or have the wrong owner: %s"
                 % (i + 1, "player's" if ours else "Nod", t))
    for ours in (0, 1):
        s = [x for x in states[ours] if x]
        seq = [x[0] for x in s]
        cl = [x[1] for x in s if x[0] == 1]
        need(seq and seq[0] == 0 and seq[-1] == 2 and seq == sorted(seq)
             and cl == list(range(cl[0], cl[0] + len(cl))) if cl else False,
             "the %s tank does not cloak in order: %s"
             % ("player's" if ours else "Nod", [x[:2] for x in s]))
    vis = {ours: [x[2] if x else None for x in states[ours]] for ours in (0, 1)}
    for want in ("indistinct", "darken", "shadowy", "ripple", "hidden"):
        need(vis[0].count(want) >= 3, "the Nod tank is %s on %d frames, want 3 or more"
             % (want, vis[0].count(want)))
    need("ripple" not in vis[1] and "hidden" not in vis[1] and vis[1].count("shadowy") >= 20,
         "the player's tank is not shadowy from stage 19 to fully cloaked: %s" % vis[1])

    def region(t, shape):
        x0, y0, x1, y1 = t[0]["rect"]
        m = (x1 - x0) / 4.0     # the shimmer reads up to 5 of the 24 pixels to either side
        h, w = shape
        return (slice(max(0, int(y0 - m)), min(h, int(y1 + m))),
                slice(max(0, int(x0 - m)), min(w, int(x1 + m))))

    def lum(a):
        return a[..., 0] * 0.299 + a[..., 1] * 0.587 + a[..., 2] * 0.114

    counts = {}
    sprite_lum, darken_lum = {0: [], 1: []}, {0: [], 1: []}
    for i, p in enumerate(P):
        f = i + 1
        a, b = img(f, "on"), img(f, "off")
        if not need(a is not None and b is not None, "frame %d: shots missing" % f):
            continue
        diff = np.abs(a - b).max(axis=2) > 0
        inside = np.zeros(diff.shape, bool)
        for ours in (0, 1):
            t = p["tanks"].get(ours)
            if not t:
                continue
            r = region(t, diff.shape)
            inside[r] = True
            v = t[0]["visual"]
            who = "player's" if ours else "Nod"
            d, on, off = diff[r], a[r], b[r]
            n = int(d.sum())
            counts.setdefault((ours, v), []).append(n)
            ground_max = lum(off).max()
            if v == "hidden":
                # (2) HIDDEN DRAWS NOTHING: the picture is the picture without the tank
                need(n == 0, "frame %d: the fully cloaked %s tank is drawn (%d pixels)"
                     % (f, who, n))
            elif v in ("normal", "indistinct"):
                # (3) the tank itself, its white hull brighter than any of the grass
                need(n >= 400 and lum(on[d]).max() > ground_max + 40,
                     "frame %d: the %s %s tank is not drawn (%d pixels)" % (f, who, v, n))
                if v == "indistinct":
                    sprite_lum[ours].append(lum(on[d]).mean())
            elif v == "darken":
                # (3) the tank, darkened over itself
                need(n >= 400, "frame %d: the darkening %s tank is not drawn (%d pixels)"
                     % (f, who, n))
                darken_lum[ours].append(lum(on[d]).mean() if n else 999)
            elif v == "shadowy":
                # (3)(4) a dark silhouette with the shimmer through it: nothing of the
                # tank's own colours, nothing brighter than the grass it stands on
                need(n >= 150 and lum(on[d]).max() <= ground_max
                     and lum(on[d]).mean() < 0.85 * lum(off[d]).mean(),
                     "frame %d: the shadowy %s tank is not a dark silhouette (%d pixels, "
                     "brightest %.0f against the grass's %.0f, mean %.0f against %.0f)"
                     % (f, who, n, lum(on[d]).max() if n else 0, ground_max,
                        lum(on[d]).mean() if n else 0, lum(off[d]).mean() if n else 0))
            elif v == "ripple":
                # (3) the shimmer alone: the grass moved about under the shape, and not
                # one colour that was not already there
                cols_off = set(map(tuple, off.reshape(-1, 3)))
                new = sum(1 for c in map(tuple, on[d]) if c not in cols_off)
                need(n >= 50 and new == 0,
                     "frame %d: the rippling %s tank is not the shimmer alone (%d pixels "
                     "moved, %d in colours the grass does not have)" % (f, who, n, new))
        # (5) NOTHING ELSE: outside the two tanks the pictures are identical
        need(int((diff & ~inside).sum()) == 0,
             "frame %d: %d pixels changed away from the tanks" % (f, int((diff & ~inside).sum())))
    for ours in (0, 1):
        if need(sprite_lum[ours] and darken_lum[ours],
                "no frame of the %s tank both drawn and darkening" % ("player's" if ours else "Nod")):
            need(max(darken_lum[ours]) < 0.75 * min(sprite_lum[ours]),
                 "the darkening %s tank is not darker than the tank: mean %.0f against %.0f"
                 % ("player's" if ours else "Nod", max(darken_lum[ours]), min(sprite_lum[ours])))
    if why:
        # the headline first: a fully cloaked tank that is drawn
        why.sort(key=lambda m: 0 if "fully cloaked" in m else 1)
        return ("0 %d claims fail: " % len(why) + "; ".join(why[:6])
                + ("; and %d more" % (len(why) - 6) if len(why) > 6 else ""))
    hid = len(counts.get((0, "hidden"), []))
    own = counts.get((1, "shadowy"), [])
    dark = max(max(darken_lum[o]) / min(sprite_lum[o]) for o in (0, 1))
    return ("1 the Nod tank is drawn through its cloaking as the 1995 engine draws it (the "
            "tank, darkened to %.2f of its brightness at most, a dark silhouette, the shimmer "
            "alone) and not at all on the %d frames it is fully cloaked; the player's own is a "
            "dark silhouette on %d frames (%d to %d pixels); nothing else on the picture moves"
            % (dark, hid, len(own), min(own), max(own)))


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "maps":
        maps(sys.argv[2])
    elif len(sys.argv) == 4 and sys.argv[1] == "check":
        print(check(sys.argv[2], sys.argv[3]))
    else:
        sys.exit(__doc__)
