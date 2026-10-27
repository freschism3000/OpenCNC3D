"""G327: the crowd limit on Enhanced footsteps (game/infantry_audio_math.h).

usage: gate_footsteps.py script OUT.txt     writes the run's script
       gate_footsteps.py check LOG          prints "1 message" or "0 reasons"

THE RUN. SCG01EA, GDI's first mission: the landing party is seven Minigunners. First ONE
of them walks north alone while the rest stand, then the whole party is banded and
marched. Every tick prints the footstep state (`infantryaudio`): steps started since boot,
steps still ringing, and the limit's own line (walkers in earshot, their level, steps
started this tick, steps held back since boot). The run needs a sound sink (--audiowav),
or the voices never finish and the ringing count means nothing.

Claims:
  (1) A LONE SOLDIER IS NOT LIMITED: while he walks alone, never more than one walker,
      every step at full level (crowd 1), not one step held back, and at least 4 played.
  (2) THE CROWD IS: with the party marching, at least 5 walkers at once; never more than
      4 steps ringing and never more than 1 started in a tick; the level on every tick is
      sqrt(3 / walkers), floored at 0.5, from 4 walkers up; steps were held back; and at
      least 8 were still played, so the party is not silenced.
"""
import math
import re
import sys

LONE_TICKS, CROWD_TICKS = 90, 150


def script(out):
    with open(out, "w") as f:
        f.write("tick 150\ncam 56 52\nlclickobj E1 10\norder 57 50\necho PHASE lone\n")
        f.write("tick 1\ninfantryaudio\n" * LONE_TICKS)
        f.write("band 200 450 1000 790\nseldump\norder 56 47\necho PHASE crowd\n")
        f.write("tick 1\ninfantryaudio\n" * CROWD_TICKS)
        f.write("quit\n")


def check(log):
    phase, rows = None, {"lone": [], "crowd": []}
    state = None
    for line in open(log, errors="replace"):
        if line.startswith("PHASE ") or line.startswith("ECHO|PHASE "):
            phase = line.split()[-1].strip()
            continue
        m = re.match(r"INFANTRYAUDIO\|state\|.*\|plays=(\d+)\|voices=(\d+)", line)
        if m:
            state = (int(m.group(1)), int(m.group(2)))
            continue
        m = re.match(r"INFANTRYAUDIO\|limit\|walkers=(\d+)\|crowd=([\d.]+)\|started=(\d+)\|held=(\d+)", line)
        if m and phase in rows and state:
            rows[phase].append(dict(plays=state[0], voices=state[1], walkers=int(m.group(1)),
                                    crowd=float(m.group(2)), started=int(m.group(3)),
                                    held=int(m.group(4))))
    bad = []
    lone, crowd = rows["lone"], rows["crowd"]
    if len(lone) < LONE_TICKS // 2 or len(crowd) < CROWD_TICKS // 2:
        return "0 the footstep dump is missing (lone %d rows, crowd %d): a binary without the " \
               "limit line, or the run stopped early" % (len(lone), len(crowd))
    # (1)
    lw = max(r["walkers"] for r in lone)
    lplays = lone[-1]["plays"] - lone[0]["plays"]
    if lw > 1:
        bad.append("%d walkers while one man walked alone" % lw)
    if any(r["crowd"] != 1.0 for r in lone):
        bad.append("a lone soldier's step played below full level")
    if lone[-1]["held"] != lone[0]["held"]:
        bad.append("%d of a lone soldier's steps were held back" % (lone[-1]["held"] - lone[0]["held"]))
    if lplays < 4:
        bad.append("a lone soldier played %d steps in %d ticks (want 4 or more)" % (lplays, len(lone)))
    # (2)
    cw = max(r["walkers"] for r in crowd)
    cv = max(r["voices"] for r in crowd)
    cs = max(r["started"] for r in crowd)
    cplays = crowd[-1]["plays"] - crowd[0]["plays"]
    cheld = crowd[-1]["held"] - crowd[0]["held"]
    if cw < 5:
        bad.append("the party never had more than %d walkers at once: the scene tests nothing" % cw)
    if cv > 4:
        bad.append("%d steps rang at once (the limit is 4)" % cv)
    if cs > 1:
        bad.append("%d steps started in one tick (the limit is 1)" % cs)
    for r in crowd:
        want = 1.0 if r["walkers"] <= 3 else max(0.5, math.sqrt(3.0 / r["walkers"]))
        if abs(r["crowd"] - want) > 0.0005:
            bad.append("%d walkers played at level %.4f (want %.4f)" % (r["walkers"], r["crowd"], want))
            break
    if cheld <= 0:
        bad.append("no step was held back while the party marched")
    if cplays < 8:
        bad.append("the party played only %d steps in %d ticks (want 8 or more)" % (cplays, len(crowd)))
    if bad:
        return "0 " + "; ".join(bad)
    return ("1 alone: %d steps, none held, full level; the party of up to %d walkers: %d steps "
            "played, %d held back, at most %d ringing and %d started a tick, level down to %.3f"
            % (lplays, cw, cplays, cheld, cv, cs, min(r["crowd"] for r in crowd)))


if __name__ == "__main__":
    if sys.argv[1] == "script":
        script(sys.argv[2])
    else:
        print(check(sys.argv[2]))
