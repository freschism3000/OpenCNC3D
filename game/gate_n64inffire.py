"""G330: with the cartridge infantry art, soldiers are seen firing, firing prone and prone.

usage: gate_n64inffire.py script OUT TAG INFSET     write one run's script
       gate_n64inffire.py check SHOTS LOG_CART LOG_DOS

THE SCENE. SCG02EA opens with a firefight: GDI and Nod riflemen fire standing from the
first ticks, lie down and fire prone. The same script runs twice in the N64 look, once
with the cartridge infantry art and once with the DOS art. The engine does not know
which art is drawn, so the two runs hold the same soldiers in the same Dos at the same
ticks, and the DOS strip each man wears names the Do he is in (the DOS pack has a strip
per Do). `animdump` prints the strip every drawn soldier wears; over ticks 3 to 10 each
frame is also shot with and without the soldiers (`infdraw 0`), which differ only where
a soldier is drawn.

Claims:
  (1) THE ENGINE'S COMBAT DOS DRAW THE CARTRIDGE'S COMBAT STRIPS. At every tick where the
      DOS run draws a soldier from FIRE, FPRONE, PRONE or CRAWL, the cartridge run draws
      that soldier from SHOOT, CSHOT, CRAWL and CRAWL, the cartridge's own table
      (bake_n64infantry.py). Not one exception, and at least 50, 15 and 30 of the first
      three are seen.
  (2) ON THE CARTRIDGE'S STAGE RULE, (stage % count) * stages / count. The scene's
      soldiers are all E1: his standing fire runs 8 frames and SHOOT has 8 stages, so the
      two stages are equal; his prone fire runs 6 and CSHOT has 8, so CSHOT's stage is
      the DOS stage * 8 // 6.
  (3) NOTHING ELSE MOVES. Every other pair draws the scenario pack's own STAND, RUN or
      DETH1 strip, as before.
  (4) THE FLASHES ARE ON SCREEN. The rifle's muzzle flash is painted into the firing
      frames; over the eight shot frames the soldiers' own pixels include at least 1000
      flash-coloured ones (R >= 200 and G >= 140; the uniforms never reach that).
"""
import collections
import os
import sys

import numpy as np
from PIL import Image

SHOT_TICKS = range(3, 11)
LAST_TICK = 120
PAIR = {"FIRE": "SHOOT", "FPRONE": "CSHOT", "PRONE": "CRAWL", "CRAWL": "CRAWL"}
NEED = {"FIRE": 50, "FPRONE": 15, "PRONE": 30}
OWN = ("STAND", "RUN", "DETH1")


def script(out, tag, infset):
    lines = ["gfxmode 1", "gfx infset %s" % infset, "cam 51 52", "zoom max", "tick 2"]
    for t in SHOT_TICKS:
        lines += ["tick 1", "animdump",
                  "shot shots/g330_%s_%d_on.png" % (tag, t), "infdraw 0",
                  "shot shots/g330_%s_%d_off.png" % (tag, t), "infdraw 1"]
    lines.append("zoom min")
    for _ in range(SHOT_TICKS[-1] + 1, LAST_TICK + 1):
        lines += ["tick 1", "animdump"]
    lines.append("quit")
    open(out, "w").write("\n".join(lines) + "\n")


def picks(log):
    """(engine frame, heap id) -> (type, action, stage) off the ANIM lines."""
    out, frame = {}, None
    for line in open(log, errors="replace"):
        if line.startswith("ANIM-BEGIN"):
            frame = int(line.split("frame=")[1].split()[0])
        elif line.startswith("ANIM|"):
            f = line.rstrip("\n").split("|")
            action = f[8].split("#")[0].split("_", 1)[1]
            out[(frame, int(f[1]))] = (f[2], action, int(f[10]))
    return out


def flash_pixels(shots, tag):
    n = 0
    for t in SHOT_TICKS:
        on = np.asarray(Image.open(os.path.join(shots, "g330_%s_%d_on.png" % (tag, t))).convert("RGB")).astype(int)
        off = np.asarray(Image.open(os.path.join(shots, "g330_%s_%d_off.png" % (tag, t))).convert("RGB")).astype(int)
        mine = (on != off).any(axis=2)
        n += int((mine & (on[:, :, 0] >= 200) & (on[:, :, 1] >= 140)).sum())
    return n


def check(shots, log_cart, log_dos):
    cart, dos = picks(log_cart), picks(log_dos)
    seen, hit = collections.Counter(), collections.Counter()
    bad = []
    for key, (ty, act, st) in sorted(dos.items()):
        if key not in cart:
            bad.append("tick %d id %d drawn only in the DOS run" % key)
            continue
        cty, cact, cst = cart[key]
        if act in PAIR:
            seen[act] += 1
            if cact != PAIR[act]:
                bad.append("tick %d id %d: DOS %s, cartridge %s" % (key + (act, cact)))
                continue
            hit[act] += 1
            if ty == "E1" and act == "FIRE" and cst != st:
                bad.append("tick %d id %d: FIRE stage %d, SHOOT stage %d" % (key + (st, cst)))
            elif ty == "E1" and act == "FPRONE" and cst != st * 8 // 6:
                bad.append("tick %d id %d: FPRONE stage %d, CSHOT stage %d" % (key + (st, cst)))
        elif cact not in OWN:
            bad.append("tick %d id %d: DOS %s drew the cartridge's %s" % (key + (act, cact)))
    short = ["%s %d < %d" % (a, seen[a], n) for a, n in NEED.items() if seen[a] < n]
    fc, fd = flash_pixels(shots, "c"), flash_pixels(shots, "d")
    what = ("%d of %d standing-fire picks wear SHOOT, %d of %d prone-fire CSHOT, %d of %d "
            "prone or crawling CRAWL, paired tick by tick with the DOS art's own Do (%d pairs "
            "in all); %d muzzle-flash pixels on the cartridge soldiers over 8 frames (DOS "
            "art: %d)" % (hit["FIRE"], seen["FIRE"], hit["FPRONE"], seen["FPRONE"],
                          hit["PRONE"] + hit["CRAWL"], seen["PRONE"] + seen["CRAWL"],
                          len(dos), fc, fd))
    if bad:
        return "0 %d mismatches, first: %s; %s" % (len(bad), bad[0], what)
    if short:
        return "0 too few seen (%s); %s" % (", ".join(short), what)
    if fc < 1000:
        return "0 only %d flash pixels on the cartridge soldiers (want 1000); %s" % (fc, what)
    return "1 " + what


if __name__ == "__main__":
    if sys.argv[1] == "script":
        script(sys.argv[2], sys.argv[3], sys.argv[4])
    else:
        print(check(sys.argv[2], sys.argv[3], sys.argv[4]))
