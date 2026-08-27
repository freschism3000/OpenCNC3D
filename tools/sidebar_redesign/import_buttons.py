"""Assemble the per-state button PNGs into native-size sprite strips.

INPUT
-----
`incoming/buttons/native/<control>_<state>.png`, one file per control per state, already
at the in-game box:

    button_repair  56x23   default hover pressed active
    button_sell    34x23   default hover pressed active
    button_map     35x23   default hover pressed
    arrow_up       32x25   default hover pressed
    arrow_down     32x25   default hover pressed

NOTHING IS RESAMPLED. Earlier rounds arrived as a single render that had to be reduced
5-7x, and before that as vertical strips whose frame boundaries could not be detected
(the pressed state sits at background luminance and the hover glow bleeds across the
gaps, so four different segmenters gave four different tile counts on the same file).
Delivering one file per state at native size removes both problems: this script only
stacks them.

The zip also carries an `8x/` copy of everything. It is kept for future editing, but the
native files are what gets baked, because reducing 8x art here would only re-introduce a
resample that the original export has already done better.

MAP has no `active`: it is momentary, not `IsToggleType`, so it never latches and the
strip is three frames. REPAIR and SELL latch and carry the fourth.

Frame order, the contract with hud640.h and cnc_sidebar.h:
    0 normal   1 hover   2 pressed   3 active (REPAIR and SELL only)
"""
from PIL import Image
import os, json

HERE = os.path.dirname(os.path.abspath(__file__))
CH = os.path.join(HERE, "chunks")
SRC = os.path.join(HERE, "incoming", "buttons", "native")

# control -> (frame size, states in frame order)
CONTROLS = {
    "button_repair": ((56, 23), ("default", "hover", "pressed", "active")),
    "button_sell":   ((34, 23), ("default", "hover", "pressed", "active")),
    "button_map":    ((35, 23), ("default", "hover", "pressed")),
    "arrow_up":      ((32, 25), ("default", "hover", "pressed")),
    "arrow_down":    ((32, 25), ("default", "hover", "pressed")),
}


def build():
    manifest = {}
    if os.path.exists(os.path.join(CH, "frames.json")):
        manifest = json.load(open(os.path.join(CH, "frames.json")))

    for name, ((w, h), states) in CONTROLS.items():
        frames = []
        for st in states:
            p = os.path.join(SRC, "%s_%s.png" % (name, st))
            if not os.path.exists(p):
                raise SystemExit("missing %s" % p)
            im = Image.open(p).convert("RGBA")
            if im.size != (w, h):
                raise SystemExit("%s is %dx%d, expected %dx%d"
                                 % (os.path.basename(p), im.width, im.height, w, h))
            frames.append(im)

        strip = Image.new("RGBA", (w * len(frames), h), (0, 0, 0, 0))
        for i, f in enumerate(frames):
            strip.alpha_composite(f, (i * w, 0))
        strip.save(os.path.join(CH, name + ".png"))

        prev = manifest.get(name, {})
        manifest[name] = dict(frames=len(frames), w=w, h=h,
                              x=prev.get("x"), y=prev.get("y"))
        print("%-14s %d frames of %dx%d  (%s)"
              % (name, len(frames), w, h, " ".join(states)))
    json.dump(manifest, open(os.path.join(CH, "frames.json"), "w"), indent=1)


if __name__ == "__main__":
    build()
