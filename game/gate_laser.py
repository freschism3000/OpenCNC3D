#!/usr/bin/env python3
"""G320's two halves: the scene it stages and the claims it checks.

    python3 gate_laser.py maps DIR            write the test mission and the first run's
                                              script (find.txt)
    python3 gate_laser.py script DIR FINDLOG  write the second run's script (laser.txt),
                                              standing on the frame the first run found
    python3 gate_laser.py check LOGDIR DIR    read the runs' logs and shots (shots/g320*)
                                              and print "1 message" or "0 why"

WHAT IS UNDER TEST. The Obelisk of Light's beam (laser_mod.h; the Classic lines in
dosclassic_mod.h). The engine reports each beam once, as EFX|LINE, on the dump of the tick
it fired, and the 1995 game draws it on that frame and the four after it (the cartridge on
the five after it). The beam must be on the picture on exactly those frames and on no other,
in every graphics mode.

THE SCENE. A copy of the skirmish map SCM01EA turned into a single-player mission, SCG96EA:
a Nod Obelisk with an Advanced Power Plant to run it, and a GDI Mammoth Tank standing in its
range (600 strength, so the Obelisk's 200 a shot leaves it standing through the window the
gate watches). The player is GDI, so the Obelisk fires on its own.

THE SCRIPTS stand on frames, not on wall time. The first run lets `ticklaser` advance one
tick at a time until the engine reports the beam, which names the frame the Obelisk fires
on. The second shoots a frame long before it (the Obelisk still charging), the frame just
before it, and that frame and the six after it (ages 0..6). Every frame is shot twice, with
the beam switched on and off (`laser 1|0`), in all three modes (`gfxmode 0|1|2`), so the
beam's pixels are exactly the difference between the two pictures of the same frame, and
"absent" means the two are byte identical. N64 shows the beam a frame longer than the
other two, six frames to their five: the cartridge's own count against the engine's.
"""
import os
import re
import sys

OBELISK = (14, 29)        # 1x2 footprint, cells (14,29) and (14,30)
POWER = (9, 29)           # Advanced Power Plant, 2x2
TANK = (18, 33)
CAM = (16.2, 31.3)
MODES = [(0, "classic"), (1, "n64"), (2, "enhanced")]
AGES = range(7)
# the frames each mode shows the beam for: the engine's LineMaxFrames, and the cartridge's
# own count under N64
SHOWS = {"classic": 5, "n64": 6, "enhanced": 5}
TAIL = ["", "[TERRAIN]", "", "[SMUDGE]", "", "[Triggers]", "", "[CellTriggers]", "",
        "[Teams]", "", "[Waypoints]", "", "[Base]", "Count=0", ""]


def cell(x, y):
    return y * 64 + x


def frames(tag, modes=MODES, extra=()):
    """the shot pair for one frame in each mode: the beam on, then off"""
    s = []
    for m, name in modes:
        s += ["gfxmode %d" % m] + list(extra) + [
              "laser 1", "shot shots/g320_%s_%s_on.png" % (tag, name), "laserdump",
              "laser 0", "shot shots/g320_%s_%s_off.png" % (tag, name), "laser 1"]
    return s


HEAD = ["tick 3", "cam %.1f %.1f" % CAM, "dist 1500", "tick 1"]


def maps(out):
    src = os.path.join("missions", "SCM01EA")
    b = open(src + ".BIN", "rb").read()
    ini = open(src + ".INI", encoding="latin-1").read()
    mapsec = re.search(r"\[MAP\].*?(?=\n\[)", ini, re.S).group(0).replace("\r", "").strip()
    os.makedirs(out, exist_ok=True)
    st = ["000=BadGuy,OBLI,256,%d,0,None" % cell(*OBELISK),
          "001=BadGuy,NUK2,256,%d,0,None" % cell(*POWER)]
    un = ["000=GoodGuy,HTNK,256,%d,0,Sticky,None" % cell(*TANK)]
    text = ["[Basic]", "Player=GoodGuy", "Action=x", "Lose=x", "Win=x", "Brief=x", "Intro=x",
            "BuildLevel=98", "Theme=No theme", "Percent=0", "CarryOverCap=-1",
            "CarryOverMoney=0", "", mapsec, "",
            "[GoodGuy]", "Allies=GoodGuy", "MaxBuilding=150", "MaxUnit=150",
            "Credits=0", "Edge=South", "", "[BadGuy]", "Allies=BadGuy", "MaxBuilding=150",
            "MaxUnit=150", "Credits=0", "Edge=North", "",
            "[STRUCTURES]"] + st + ["", "[UNITS]"] + un + TAIL
    open(os.path.join(out, "SCG96EA.INI"), "w", encoding="latin-1",
         newline="").write("\r\n".join(text))
    open(os.path.join(out, "SCG96EA.BIN"), "wb").write(b)
    # THE FIRST RUN only finds the frame the Obelisk fires on, so the second can stand on
    # the frame before it
    open(os.path.join(out, "find.txt"), "w").write(
        "\n".join(HEAD + ["laserdump", "ticklaser 900", "laserdump", "quit"]) + "\n")


def rd(path):
    try:
        with open(path, errors="ignore") as f:
            return f.read().splitlines()
    except OSError:
        return []


def fired_frame(log):
    for l in rd(log):
        m = re.match(r"TICKLASER\|ticks=(\d+)\|frame=(\d+)\|.*\|fired$", l)
        if m:
            return int(m.group(2))
    return None


def start_frame(log):
    for l in rd(log):
        m = re.match(r"LASER\|frame=(\d+)\|", l)
        if m:
            return int(m.group(1))
    return None


def script(out, findlog):
    """THE SECOND RUN: the pre frame, the frame before the shot, the shot's frame and the
    five after it, every one in all three modes with the beam on and off, and the shot's
    frame once more under Enhanced with the bloom off"""
    f, f0 = fired_frame(findlog), start_frame(findlog)
    if f is None or f0 is None or f - 1 <= f0:
        return "0 the first run did not see the Obelisk fire (%s)" % findlog
    s = HEAD + frames("pre") + ["tick %d" % (f - 1 - f0), "laserdump"] + frames("b1")
    for age in AGES:
        s += ["tick 1"] + frames("a%d" % age)
        if age == 0:
            s += frames("a0nb", [(2, "enhanced")], ["gfx bloom_on 0"]) + ["gfx bloom_on 1"]
    s += ["quit"]
    open(os.path.join(out, "laser.txt"), "w").write("\n".join(s) + "\n")
    return "1 fires on frame %d" % f


def check(logs, d):
    from PIL import Image
    import numpy as np
    why = []

    def need(c, m):
        if not c:
            why.append(m)
        return c

    def img(n):
        p = os.path.join(logs, "g320_%s.png" % n)
        return np.asarray(Image.open(p).convert("RGB")).astype(np.int64) if os.path.exists(p) else None

    L = rd(os.path.join(logs, "g320.log"))
    F = fired_frame(os.path.join(logs, "g320find.log"))
    need(F is not None, "the first run never saw the Obelisk fire")
    dumps = [l for l in L if l.startswith("LASER|frame=")]
    beams = [l for l in L if l.startswith("LASER|beam|")]
    need(len(dumps) == 3 * (2 + len(AGES)) + 2,
         "%d LASER dump lines, want %d" % (len(dumps), 3 * (2 + len(AGES)) + 2))

    def field(l, k):
        m = re.search(r"\|%s=([^|]*)" % k, l)
        return m.group(1) if m else None

    # (1) ENGINE TRUTH: one beam, born on the frame the first run saw it fire, with the
    # engine's own lines: 5 frames, colours 0x7D 0x7D 0x7F, offsets +1 -1 0, an Obelisk's
    born = sorted(set(field(l, "born") for l in beams))
    need(F is not None and born == [str(F)],
         "the beam was born on %s, the Obelisk fired on frame %s" % (born, F))
    need(beams and all(field(l, "type") == "OBLI" and field(l, "frames") == "5"
                       and field(l, "colour") == "125,125,127" and field(l, "off") == "1,-1,0"
                       for l in beams),
         "the beam is not the engine's three lines for five frames: %s" % (beams[:1],))
    need(all(field(l, "noted") == "0" for l in dumps[:4]),
         "a beam was on the list before the shot: %s" % dumps[:4])
    rgb = None
    for l in dumps:
        if field(l, "mode") == "classic" and field(l, "drawn") not in ("0", None):
            rgb = [tuple(int(v) for v in t.split(",")) for t in field(l, "classicrgb").split(";")]

    def ends(age, mode):
        """the beam's two ends in window pixels, as the dump of that frame and mode said"""
        cand = [b for b, dl in zip(range(len(L)), L) if dl.startswith("LASER|beam|")]
        for i in cand:
            if field(L[i], "age") == str(age):
                # the dump line just before names the mode
                j = i - 1
                while j >= 0 and not L[j].startswith("LASER|frame="):
                    j -= 1
                if j >= 0 and field(L[j], "mode") == mode:
                    s = [float(v) for v in field(L[i], "Spx").split(",")]
                    t = [float(v) for v in field(L[i], "Tpx").split(",")]
                    return s, t
        return None

    def corridor(shape, s, t, r):
        h, w = shape
        yy, xx = np.mgrid[0:h, 0:w]
        dx, dy = t[0] - s[0], t[1] - s[1]
        ll = max(dx * dx + dy * dy, 1e-6)
        u = np.clip(((xx - s[0]) * dx + (yy - s[1]) * dy) / ll, 0, 1)
        px, py = s[0] + u * dx, s[1] + u * dy
        return (xx - px) ** 2 + (yy - py) ** 2 <= r * r

    report = []
    for m, name in MODES:
        side = 1120 if name == "classic" else 1040 if name == "n64" else 1090
        # (2) NOT BEFORE: long before the shot and on the frame just before it, the picture
        # with the beam on is the picture with it off, byte for byte
        for tag in ("pre", "b1"):
            a, b = img("%s_%s_on" % (tag, name)), img("%s_%s_off" % (tag, name))
            need(a is not None and b is not None and np.array_equal(a, b),
                 "%s: the %s frame differs with the beam switched on (%s pixels)"
                 % (name, tag, None if a is None or b is None
                    else int((np.abs(a - b).max(axis=2) > 0).sum())))
        # (3) ON THE SHOT'S FRAME AND THE FOUR AFTER: the beam's pixels, along the beam
        counts = []
        for age in range(SHOWS[name]):
            a, b = img("a%d_%s_on" % (age, name)), img("a%d_%s_off" % (age, name))
            if a is None or b is None:
                need(False, "%s: age %d shots missing" % (name, age))
                continue
            diff = np.abs(a - b).max(axis=2) > 0
            n = int(diff.sum())
            counts.append(n)
            e = ends(age, name)
            if not need(e is not None, "%s: no ends dumped at age %d" % (name, age)):
                continue
            near = corridor(diff.shape, e[0], e[1], 12 if name != "enhanced" else 14)
            if name != "enhanced":
                need(n >= 300 and (diff & ~near).sum() == 0,
                     "%s age %d: %d beam pixels, %d of them off the line from %s to %s"
                     % (name, age, n, int((diff & ~near).sum()), e[0], e[1]))
            else:
                # the core: white-hot pixels on the line; the glow and the bloom around it
                # (the impact's flare is wider than the beam: a disc round the far end).
                # White-hot means the beam MADE the pixel so: one that was nearly white
                # already (a pale marking on the target, which the beam's own red light
                # takes to 255) is lit, not the beam's core.
                hot = diff & (a[:, :, 0] >= 250) & (a[:, :, 1] >= 150) & (b.max(axis=2) < 200)
                near |= corridor(diff.shape, e[1], e[1], 60)
                need(int((hot & near).sum()) >= (200 if age < 3 else 20)
                     and int((hot & ~near).sum()) == 0,
                     "enhanced age %d: %d white-hot pixels on the line, %d off it"
                     % (age, int((hot & near).sum()), int((hot & ~near).sum())))
                red = a[diff].mean(axis=0) - b[diff].mean(axis=0) if n else [0, 0, 0]
                need(n and red[0] > red[1] and red[0] > red[2],
                     "enhanced age %d: the beam does not add red (%s)" % (age, red))
            if name == "n64":
                cols = set(map(tuple, a[diff]))
                need(cols == {(255, 10, 10)},
                     "n64 age %d: beam pixels in %s, the cartridge draws (255, 10, 10) flat"
                     % (age, sorted(cols)[:4]))
            if name == "classic" and rgb:
                cols = set(map(tuple, a[diff]))
                need(cols <= set(rgb) and rgb[2] in cols,
                     "classic age %d: beam pixels in %s, the palette gives %s"
                     % (age, sorted(cols)[:4], rgb))
            need(diff[:, side:].sum() == 0, "%s age %d: the beam reached the sidebar" % (name, age))
        # (4) AND GONE AFTER: identical again on every later frame shot
        for age in range(SHOWS[name], len(AGES)):
            a, b = img("a%d_%s_on" % (age, name)), img("a%d_%s_off" % (age, name))
            need(a is not None and b is not None and np.array_equal(a, b),
                 "%s: the beam is still on the picture %d frames after the shot" % (name, age))
        report.append("%s %s" % (name, "/".join(str(c) for c in counts)))
    # (5) THE BLOOM TAKES IT: under Enhanced the beam moves more of the picture with the
    # bloom on than off, and the extra lies beside the line
    a, b = img("a0_enhanced_on"), img("a0_enhanced_off")
    na, nb_ = img("a0nb_enhanced_on"), img("a0nb_enhanced_off")
    if a is not None and na is not None:
        with_b = int((np.abs(a - b).max(axis=2) > 8).sum())
        without = int((np.abs(na - nb_).max(axis=2) > 8).sum())
        need(with_b > 1.3 * without and without > 0,
             "the bloom did not take the beam: %d pixels moved by more than 8 with it, %d "
             "without" % (with_b, without))
        report.append("bloom %d vs %d" % (with_b, without))
    if why:
        return "0 " + "; ".join(why)
    return "1 fired on frame %d; drawn on frames %d..%d in Classic and Enhanced and %d..%d " \
           "under N64, and on no other (beam pixels by age: %s)" \
           % (F, F, F + SHOWS["classic"] - 1, F, F + SHOWS["n64"] - 1, "; ".join(report))


if __name__ == "__main__":
    if len(sys.argv) >= 3 and sys.argv[1] == "maps":
        maps(sys.argv[2])
    elif len(sys.argv) >= 4 and sys.argv[1] == "script":
        print(script(sys.argv[2], sys.argv[3]))
    elif len(sys.argv) >= 4 and sys.argv[1] == "check":
        print(check(sys.argv[2], sys.argv[3]))
    else:
        sys.exit(__doc__)
