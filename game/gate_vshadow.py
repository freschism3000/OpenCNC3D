#!/usr/bin/env python3
"""G28's pixel half: did the vehicle shadows darken the ground, and only darken it?

Kept as its own file rather than a heredoc inside gates.sh. A nested heredoc inside a
shell block is exactly the kind of quoting that breaks silently, and the point of this
suite is checks that cannot pass by accident.

Prints three numbers: pixels-changed lighter-pixels worst-darkening.
"lighter" is the load-bearing one. A count-only check passes just as happily on a wrong
combiner, a lost blend or an opaque black quad; a shadow that ever LIGHTENS a pixel is
none of those things and must fail.
"""
import sys

from PIL import Image
import numpy as np


def main():
    on, off = sys.argv[1], sys.argv[2]
    a = np.asarray(Image.open(on).convert("RGB")).astype(int)
    b = np.asarray(Image.open(off).convert("RGB")).astype(int)
    if a.shape != b.shape:
        print("0 999999 0")
        return 1
    d = (a != b).any(axis=2)
    n = int(d.sum())
    if not n:
        print("0 0 0")
        return 0
    delta = (b - a)[d]                      # positive = the shadow DARKENED it
    lighter = int((delta < 0).any(axis=1).sum())
    worst = int(delta.max())
    print("%d %d %d" % (n, lighter, worst))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
