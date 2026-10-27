#!/usr/bin/env python3
"""G305's two halves: the scene it stages and the claims it checks.

    python3 gate_structs.py maps DIR        write the test mission and its scripts
    python3 gate_structs.py check LOGS DIR  read the runs' logs and shots (shots/g305*)
                                            and print "1 message" or "0 why"

WHAT IS UNDER TEST. The F5 panel's STRUCTURES group (fx_state.h, 12p): eight dials that
grade and light the buildings and walls under Enhanced and nothing else, read in the light
pass (fx_post.h) off the class the normal target carries, with the cartridge's own
buildings and walls marked there by str_draw_class (cnc_eyes.cpp).

THE SCENE. A copy of the skirmish map SCM01EA turned into a single-player mission,
SCG95EA, holding in one view: an Obelisk and a Temple of Nod (remasters that carry self-lit
lamps, lenses and glass), two civilian houses (V01, V02) and a run each of barbed wire and
wooden fence (remasters too, since format 6; under --noebld the cartridge's), and a Medium Tank
and a squad of riflemen beside them, which the dials must leave alone. Every structure is
the player's or neutral, so nothing fires.

THE SCRIPT shoots the same tick over and over, one dial at a time moved off neutral and
put back: a frame for every dial with the bloom on (the shipped look) and again with it
off, because the bloom spreads a structure's change onto the ground beside it, which is
the bloom doing its job and not the group reaching the ground. The structure's own pixels
are found by the group itself: brightness 0 takes every structure to black and nothing
else, so the pixels that moved are the mask.
"""
import json
import os
import re
import sys

STRUCTS = [("GoodGuy", "OBLI", 14, 29), ("GoodGuy", "TMPL", 16, 29),
           ("Neutral", "V01", 20, 30), ("Neutral", "V02", 22, 30)]
WALLS = {"BARB": [(x, 33) for x in range(14, 18)], "WOOD": [(x, 33) for x in range(19, 23)]}
UNIT = (18, 35)
SQUAD = (20, 35)
CAM = (18.5, 32.5)
# (dial, value, what the structure's pixels must do): brighter, darker, flatter, greyer,
# or the self-lit pixels darker
DIALS = [("st_gamma", "1.8", "brighter"), ("st_bright", "1.4", "brighter"),
         ("st_contrast", "1.6", "spread"), ("st_sat", "0", "greyer"),
         ("st_shade", "2", "darker"), ("st_sun", "1.8", "brighter"),
         ("st_glow", "0", "glowdark"), ("st_ao", "2", "darker")]
# Every run starts with all eight dials at neutral, whatever the shipped defaults are,
# because the gate tests the group's mechanism from its neutral path.
NEUTRAL = ["gfx %s 1" % k for k, _, _ in DIALS]
# The independent self-shadow fill also needs structure classification. Isolate the
# eight grade/light dials from it, then exercise that separate class-mask user below.
NEUTRAL += ["gfx object_shadow_fill 0"]
# AND THE FRAME'S GRADE IS THE ONE THE CLAIMS WERE MEASURED UNDER, with the moving mist
# off, because "greyer" is read off the finished frame, after the grade. At st_sat 0 the
# structures came out a neutral grey (82, 82, 83 on average) under this grade and a
# blue grey (66, 72, 78) under the cooler shipped one, so their chroma fell only to 0.502
# of its neutral value against a bar of 0.5 with the dial doing what it always did.
# Under this grade it falls to 0.25.
GRADE = ["gfx exposure -0.25", "gfx contrast 1.15625", "gfx saturation 1.03571",
         "gfx temperature 0.125", "gfx tint 0", "gfx lift 0.00357", "gfx gain 1.59821",
         "gfx bloom_intensity 0.28571", "gfx fog_fx 0"]
NEUTRAL += GRADE
# AND THE OCCLUSION AS IT WAS MEASURED. The structures' mask is every pixel that changes
# when the buildings are hidden, so a building pixel that happens to equal the ground under
# it falls outside the mask; under the stronger shipped occlusion one such pixel turned up
# (18, 17, 17 over 18, 17, 17) and was counted as a pixel outside the structures that
# brightness 0 had moved.
NEUTRAL += ["gfx ssao_intensity 0.51786"]
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
    st = ["%03d=%s,%s,256,%d,0,None" % (i, h, t, cell(x, y))
          for i, (h, t, x, y) in enumerate(STRUCTS)]
    ov = ["%d=%s" % (cell(x, y), k) for k in WALLS for x, y in WALLS[k]]
    un = ["000=GoodGuy,MTNK,256,%d,0,Guard,None" % cell(*UNIT)]
    inf = ["%03d=GoodGuy,E1,256,%d,%d,Guard,0,None" % (i, cell(*SQUAD), i + 1)
           for i in range(3)]
    text = ["[Basic]", "Player=GoodGuy", "Action=x", "Lose=x", "Win=x", "Brief=x", "Intro=x",
            "BuildLevel=98", "Theme=No theme", "Percent=0", "CarryOverCap=-1",
            "CarryOverMoney=0", "", mapsec, "",
            "[GoodGuy]", "Allies=GoodGuy,Neutral", "MaxBuilding=150", "MaxUnit=150",
            "Credits=0", "Edge=South", "", "[BadGuy]", "Allies=BadGuy", "MaxBuilding=150",
            "MaxUnit=150", "Credits=0", "Edge=North", "", "[Neutral]", "Allies=Neutral,GoodGuy",
            "MaxBuilding=150", "MaxUnit=150", "Credits=0", "Edge=North", "",
            "[STRUCTURES]"] + st + ["", "[UNITS]"] + un + ["", "[INFANTRY]"] + inf + \
           ["", "[OVERLAY]"] + ov + TAIL
    open(os.path.join(out, "SCG95EA.INI"), "w", encoding="latin-1",
         newline="").write("\r\n".join(text))
    open(os.path.join(out, "SCG95EA.BIN"), "wb").write(b)
    # THE ENHANCED SCRIPT: neutral, every dial with the bloom on, then the mask and every
    # dial with it off, then neutral again after all of it
    s = ["tick 5", "cam %.1f %.1f" % CAM, "dist 2000", "tick 1"] + NEUTRAL + [
         "shot shots/g305_n0.png", "stdump"]
    for blo in ("", "nb_"):
        if blo:
            s += ["gfx bloom_on 0", "shot shots/g305_nb_n.png",
                  "bldshow 0", "walls 0", "shot shots/g305_nb_bare.png", "bldshow 1",
                  "walls 1", "gfx st_bright 0", "shot shots/g305_nb_mask.png", "stdump",
                  "gfx st_bright 1"]
        for key, val, _ in DIALS:
            s += ["gfx %s %s" % (key, val), "shot shots/g305_%s%s.png" % (blo, key),
                  "stdump", "gfx %s 1" % key]
    s += ["gfx bloom_on 1", "shot shots/g305_n1.png", "stdump",
          "gfx object_shadow_fill 0.5", "shot shots/g305_fill_on.png", "stdump",
          "gfx object_shadow_fill 0", "shot shots/g305_fill_reset.png", "stdump", "quit"]
    open(os.path.join(out, "enh.txt"), "w").write("\n".join(s) + "\n")
    # --noebld (every structure the cartridge's) and Classic: neutral, then brightness 0
    for tag in ("x", "k"):
        s = ["tick 5", "cam %.1f %.1f" % CAM, "dist 2000", "tick 1"] + NEUTRAL + [
             "gfx bloom_on 0",
             "shot shots/g305%s_n.png" % tag, "stdump", "bldshow 0", "walls 0",
             "shot shots/g305%s_bare.png" % tag, "bldshow 1", "walls 1", "gfx st_bright 0",
             "shot shots/g305%s_mask.png" % tag, "stdump", "quit"]
        open(os.path.join(out, "%s.txt" % tag), "w").write("\n".join(s) + "\n")


def rd(path):
    try:
        with open(path, errors="ignore") as f:
            return f.read().splitlines()
    except OSError:
        return []


def check(logs, d):
    from PIL import Image
    import numpy as np
    W = 1120                     # the tactical view; the sidebar is right of it
    GLOW_MIN = 200               # the lamps, lenses and glass of the Obelisk and Temple

    def img(n):
        return np.asarray(Image.open(os.path.join(logs, "g305%s.png" % n))
                          .convert("RGB")).astype(np.int64)

    def moved(a, b):
        return np.abs(a - b).max(axis=2) > 0

    why = []

    def need(c, m):
        if not c:
            why.append(m)
        return c

    E = rd(os.path.join(logs, "g305e.log"))
    X = rd(os.path.join(logs, "g305x.log"))
    K = rd(os.path.join(logs, "g305k.log"))
    all_st = [l for l in E if l.startswith("STRUCT|")]
    expected_grade_rows = 2 + 2 * len(DIALS) + 1
    st, fill_probe = all_st[:expected_grade_rows], all_st[expected_grade_rows:]
    on = [int(re.search(r"\|on=(\d)", l).group(1)) for l in st]
    cart = [int(re.search(r"cartdraws=(\d+)", l).group(1)) for l in st]
    skips = [int(re.search(r"cartskips=(\d+)", l).group(1)) if "cartskips=" in l else 0 for l in st]
    covered = [a + b for a, b in zip(cart, skips)]
    need(len(st) == 2 + 2 * len(DIALS) + 1,
         "%d STRUCT dump lines in the Enhanced run, want %d" % (len(st), 2 + 2 * len(DIALS) + 1))
    need(st and on[0] == 0 and on[-1] == 0 and all(on[1:-1]),
         "the light pass's structures path was not off at neutral and on while a dial "
         "moved: %s" % on)
    need(st and covered[0] == 0 and min(covered[1:-1] or [0]) >= len(STRUCTS),
         "structure classification was not supplied by a redraw or the remaster normals "
         "(covered %s, want 0 at neutral and >= %d)" % (covered, len(STRUCTS)))
    need(len(fill_probe) == 2 and "|on=1|active=1|" in fill_probe[0]
         and "|on=0|active=0|" in fill_probe[1]
         and sum(int(re.search(r"%s=(\d+)" % key, fill_probe[0]).group(1))
                 for key in ("cartdraws", "cartskips")) >= len(STRUCTS)
         and all("|%s=1.000" % key in row for row in fill_probe
                 for key in ("gamma", "bright", "contrast", "sat", "shade", "sun", "glow", "ao"))
         and "|cartdraws=0|cartskips=0|" in fill_probe[1],
         "the independent self-shadow fill did not activate structure classification "
         "with all eight dials neutral, then stand down after reset: %s" % fill_probe)

    n0, n1, nb = img("_n0"), img("_n1"), img("_nb_n")
    need(np.array_equal(n0, n1), "the neutral frame after every dial had been moved and put "
         "back differs from the first by %d pixels" % moved(n0, n1).sum())
    need(np.array_equal(n0, img("_fill_reset")),
         "the neutral frame after independent self-shadow fill reset differs from the first")
    # WHAT IN THE FRAME IS A STRUCTURE'S AT ALL, found without the group: the pixels that
    # change when the buildings and the walls are not drawn (their own and the shadows
    # they cast). Every dial must stay inside it.
    foot = moved(nb, img("_nb_bare"))
    # (a structure's pixel that happens to be the very colour of the ground behind it does
    # not move when the structures are hidden, so the footprint misses it while the group
    # rightly reaches it: measured, one pixel of the remastered fences at 1280x720. A
    # handful such is the measurement's blind spot, not the group leaking)
    STRAY = 8
    foot[:, W:] = False
    mask = moved(nb, img("_nb_mask"))
    side = mask[:, W:].sum()
    mask[:, W:] = False
    total = mask.sum()
    need(side == 0, "brightness 0 changed %d sidebar pixels" % side)
    need(30000 <= total <= 0.6 * W * nb.shape[0] and total >= 0.5 * foot.sum()
         and (mask & ~foot).sum() <= STRAY,
         "brightness 0 took %d pixels to black, %d of them outside the %d the structures "
         "cover, want 30000 to 60%% of the view and at least half of what they cover"
         % (total, (mask & ~foot).sum(), foot.sum()))
    said = []
    for blo in ("", "nb_"):
        base = n0 if not blo else nb
        for key, val, want in DIALS:
            a = img("_%s%s" % (blo, key))
            m = moved(base, a)
            ins, outs = (m & foot)[:, :W].sum(), (m & ~foot)[:, :W].sum()
            need(m[:, W:].sum() == 0, "%s %s moved the sidebar" % (blo, key))
            if blo:
                need(outs <= STRAY, "%s=%s changed %d pixels that are not a structure's "
                     "(bloom off)" % (key, val, outs))
            need(ins >= (GLOW_MIN if want == "glowdark" else 10000),
                 "%s%s=%s changed only %d of the structures' pixels" % (blo, key, val, ins))
            if not blo:
                continue
            sel = m & foot
            if not sel.any():
                continue
            before, after = base[sel], a[sel]
            lb, la = before.mean(), after.mean()
            ok = {"brighter": la > lb + 4, "darker": la < lb - 2,
                  "glowdark": la < lb - 8,
                  "spread": np.abs(after - 128).mean() > np.abs(before - 128).mean() + 4,
                  "greyer": (after.max(axis=1) - after.min(axis=1)).mean()
                  < 0.5 * (before.max(axis=1) - before.min(axis=1)).mean()}[want]
            need(ok, "%s=%s did not make the structures %s (mean %.1f -> %.1f)"
                 % (key, val, want, lb, la))
            said.append("%s %d px" % (key[3:], ins))
    # every structure the cartridge's: still reached, and nothing else
    xn = img("x_n")
    xf = moved(xn, img("x_bare"))
    xm = moved(xn, img("x_mask"))
    xf[:, W:] = False
    xs = [l for l in X if l.startswith("STRUCT|")]
    need(xm[:, :W].sum() >= 30000 and xm[:, :W].sum() >= 0.3 * xf.sum()
         and (xm & ~xf)[:, :W].sum() == 0 and xs and "cartdraws=0" in xs[0]
         and int(re.search(r"cartdraws=(\d+)", xs[-1]).group(1)) >= len(STRUCTS),
         "--noebld: brightness 0 took %d pixels to black, %d outside the %d the structures "
         "cover %s" % (xm[:, :W].sum(), (xm & ~xf)[:, :W].sum(), xf.sum(), xs))
    # Classic: the light pass does not exist, so nothing moves
    km = moved(img("k_n"), img("k_mask"))
    ks = [l for l in K if l.startswith("STRUCT|")]
    need(km.sum() == 0 and ks and all("|on=0|" in l for l in ks),
         "Classic: brightness 0 moved %d pixels %s" % (km.sum(), ks))
    if why:
        more = " (and %d more)" % (len(why) - 6) if len(why) > 6 else ""
        print("0 " + "; ".join(why[:6]) + more)
    else:
        print("1 every dial moved the structures (bloom off: %s) the way it says and not one "
              "other pixel; the structures' mask %d px, %d of it with every structure the "
              "cartridge's; neutral took the old path (on=0, nothing drawn into the target) "
              "and the frame after every dial was put back is the first frame byte for "
              "byte; independent self-shadow fill classified the structures with all eight "
              "dials neutral and reset exactly; Classic untouched"
              % (", ".join(said), total, xm[:, :W].sum()))


if __name__ == "__main__":
    if sys.argv[1] == "maps":
        maps(sys.argv[2])
    else:
        check(sys.argv[2], sys.argv[3])
